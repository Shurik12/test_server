#include <spdlog/spdlog.h>
#include <algorithm>
#include <thread>

#include <server/RequestHandler.h>
#include <logging/Logger.h>

RequestHandler::RequestHandler()
{
	Logger::info("RequestHandler initialized");
}

RequestHandler::~RequestHandler()
{
	Logger::info("RequestHandler shutting down. Statistics: {} total, {} successful, {} failed",
				 requests_processed_.load(), successful_requests_.load(), failed_requests_.load());
}

UserData RequestHandler::parseJson(const std::string &json_input)
{
	rapidjson::Document doc;
	doc.Parse(json_input.c_str());

	if (doc.HasParseError())
	{
		Logger::error("JSON parse error: {} at offset {}", doc.GetParseError(), doc.GetErrorOffset());
		throw std::runtime_error("Invalid JSON format");
	}

	if (!doc.IsObject())
	{
		throw std::runtime_error("Expected JSON object");
	}

	UserData data;

	// Parse id
	if (doc.HasMember("id") && doc["id"].IsInt())
	{
		data.id = doc["id"].GetInt();
	}
	else
	{
		throw std::runtime_error("Missing or invalid 'id' field");
	}

	// Parse name
	if (doc.HasMember("name") && doc["name"].IsString())
	{
		data.name = doc["name"].GetString();
	}
	else
	{
		throw std::runtime_error("Missing or invalid 'name' field");
	}

	// Parse phone
	if (doc.HasMember("phone") && doc["phone"].IsString())
	{
		data.phone = doc["phone"].GetString();
	}
	else
	{
		throw std::runtime_error("Missing or invalid 'phone' field");
	}

	// Parse number
	if (doc.HasMember("number") && doc["number"].IsInt())
	{
		data.number = doc["number"].GetInt();
	}
	else
	{
		throw std::runtime_error("Missing or invalid 'number' field");
	}

	return data;
}

bool RequestHandler::validateUserData(const UserData &data)
{
	if (data.name.empty())
	{
		return false;
	}

	if (data.phone.empty())
	{
		return false;
	}

	if (data.id < 0)
	{
		return false;
	}

	return true;
}

std::string RequestHandler::generateJsonResponse(const UserData &data)
{
	rapidjson::Document doc;
	doc.SetObject();
	rapidjson::Document::AllocatorType &allocator = doc.GetAllocator();

	doc.AddMember("id", data.id, allocator);
	doc.AddMember("name", rapidjson::Value(data.name.c_str(), allocator), allocator);
	doc.AddMember("phone", rapidjson::Value(data.phone.c_str(), allocator), allocator);
	doc.AddMember("number", data.number, allocator);
	doc.AddMember("success", true, allocator);

	rapidjson::StringBuffer buffer;
	rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
	doc.Accept(writer);

	return buffer.GetString();
}

std::string RequestHandler::generateErrorResponse(const std::string &error_message)
{
	rapidjson::Document doc;
	doc.SetObject();
	rapidjson::Document::AllocatorType &allocator = doc.GetAllocator();

	doc.AddMember("error", rapidjson::Value(error_message.c_str(), allocator), allocator);
	doc.AddMember("success", false, allocator);

	rapidjson::StringBuffer buffer;
	rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
	doc.Accept(writer);

	return buffer.GetString();
}

int RequestHandler::increase(int number)
{
	// Simulate some processing time
	std::this_thread::sleep_for(std::chrono::milliseconds(1));
	Logger::debug("Increasing number from {} to {}", number, number + 1);
	return ++number;
}

std::string RequestHandler::processRequestInternal(const std::string &json_input)
{
	requests_processed_++;

	try
	{
		// Parse input
		UserData user_data = parseJson(json_input);

		// Validate data
		if (!validateUserData(user_data))
		{
			throw std::runtime_error("Invalid user data");
		}

		Logger::debug("Parsed data - id: {}, name: {}, phone: {}, number: {}",
					  user_data.id, user_data.name, user_data.phone, user_data.number);

		// Track the original number before processing
		int original_number = user_data.number;

		// Perform calculation
		user_data.number = increase(user_data.number);

		// Update number tracking - use client IP or user ID as client identifier
		std::string client_id = "user_" + std::to_string(user_data.id);

		// Fix: Use atomic fetch_add for thread safety
		total_numbers_sum_.fetch_add(original_number, std::memory_order_relaxed);

		{
			std::lock_guard<std::mutex> lock(client_mutex_);
			client_numbers_sum_[client_id] += static_cast<long long>(original_number);
		}

		// Generate response
		std::string response = generateJsonResponse(user_data);
		Logger::debug("Generated response: {}", response);

		successful_requests_++;
		return response;
	}
	catch (const std::exception &e)
	{
		Logger::error("Error processing request: {}", e.what());
		failed_requests_++;
		return generateErrorResponse(e.what());
	}
}

std::string RequestHandler::processRequest(const std::string &json_input)
{
	return processRequestInternal(json_input);
}

std::future<std::string> RequestHandler::processRequestAsync(const std::string &json_input)
{
	return std::async(std::launch::async, [this, json_input]()
					  { return processRequestInternal(json_input); });
}

std::vector<std::string> RequestHandler::processBatchRequests(const std::vector<std::string> &json_inputs)
{
	std::vector<std::future<std::string>> futures;
	std::vector<std::string> results;

	for (const auto &json_input : json_inputs)
		futures.push_back(processRequestAsync(json_input));

	for (auto &future : futures)
		results.push_back(future.get());

	return results;
}

void RequestHandler::resetStatistics()
{
	requests_processed_ = 0;
	successful_requests_ = 0;
	failed_requests_ = 0;
	Logger::info("Statistics reset");
}

std::shared_ptr<DocumentData> RequestHandler::Process(std::shared_ptr<DocumentData> input)
{
	std::shared_ptr<DocumentData> correct_doc;
	std::shared_ptr<DocumentData> from_db_element;
	if (auto search = document_data_cache_.find(input->Url); search != document_data_cache_.end())
	{
		correct_doc = search->second;
		if (input->FetchTime <= correct_doc->FetchTime)
		{
			correct_doc->PubDate = input->PubDate;
			input->Text = correct_doc->Text;
			correct_doc->FirstFetchTime = input->FetchTime;
			input->FetchTime = correct_doc->FetchTime;
		}
		else
		{
			input->PubDate = correct_doc->PubDate;
			correct_doc->Text = input->Text;
			correct_doc->FetchTime = input->FetchTime;
		}
	}
	else if (from_db_element = search_in_db(input) != nullptr)
	{
		correct_doc = from_db_element;
		if (input->FetchTime <= correct_doc->FetchTime)
		{
			correct_doc->PubDate = input->PubDate;
			input->Text = correct_doc->Text;
			correct_doc->FirstFetchTime = input->FetchTime;
			input->FetchTime = correct_doc->FetchTime;
		}
		else
		{
			input->PubDate = correct_doc->PubDate;
			correct_doc->Text = input->Text;
			correct_doc->FetchTime = input->FetchTime;
		}
		add_to_update_to_db(correct_doc);
	}
	else
	{
		input->FirstFetchTime = input->FetchTime;
		if (document_data_cache_.size() >= max_document_data_cache_size_)
		{
			add_to_write_to_db(input);
		}
		else 
		{
			document_data_cache_[input->Url] = input;
		}
	}
}