#pragma once

#include <atomic>
#include <string>
#include <sstream>
#include <mutex>
#include <chrono>

class Metrics
{
public:
	static Metrics &getInstance()
	{
		static Metrics instance;
		return instance;
	}

	// Request metrics
	void incrementRequests() { requests_total_++; }
	void incrementSuccessfulRequests() { requests_successful_++; }
	void incrementFailedRequests() { requests_failed_++; }

	// Connection metrics
	void incrementConnections()
	{
		connections_total_++;
		active_connections_++;
	}
	void decrementConnections()
	{
		if (active_connections_ > 0)
			active_connections_--;
	}
	void setActiveConnections(int count) { active_connections_ = count; }

	// Timing metrics
	void updateRequestDuration(double duration_seconds)
	{
		std::lock_guard<std::mutex> lock(duration_mutex_);
		request_duration_seconds_ = duration_seconds;
	}

	void updateRequestDurationHistogram(double duration_seconds)
	{
		std::lock_guard<std::mutex> lock(histogram_mutex_);
		// Simple histogram buckets (in seconds)
		if (duration_seconds < 0.001)
			request_duration_bucket_1ms_++;
		else if (duration_seconds < 0.01)
			request_duration_bucket_10ms_++;
		else if (duration_seconds < 0.1)
			request_duration_bucket_100ms_++;
		else if (duration_seconds < 1.0)
			request_duration_bucket_1s_++;
		else
			request_duration_bucket_inf_++;

		request_duration_sum_ = request_duration_sum_ + duration_seconds;
		request_duration_count_++;
	}

	// Throughput metrics
	void incrementBytesReceived(size_t bytes) { bytes_received_ += bytes; }
	void incrementBytesSent(size_t bytes) { bytes_sent_ += bytes; }

	// Reset metrics (useful for testing)
	void reset()
	{
		requests_total_ = 0;
		requests_successful_ = 0;
		requests_failed_ = 0;
		connections_total_ = 0;
		active_connections_ = 0;
		bytes_received_ = 0;
		bytes_sent_ = 0;

		std::lock_guard<std::mutex> lock1(duration_mutex_);
		std::lock_guard<std::mutex> lock2(histogram_mutex_);
		request_duration_seconds_ = 0.0;
		request_duration_bucket_1ms_ = 0;
		request_duration_bucket_10ms_ = 0;
		request_duration_bucket_100ms_ = 0;
		request_duration_bucket_1s_ = 0;
		request_duration_bucket_inf_ = 0;
		request_duration_sum_ = 0.0;
		request_duration_count_ = 0;
	}

	std::string getPrometheusMetrics()
	{
		std::stringstream ss;

		// Counter metrics
		ss << "# HELP cpp_service_requests_total Total number of HTTP requests\n";
		ss << "# TYPE cpp_service_requests_total counter\n";
		ss << "cpp_service_requests_total " << requests_total_ << "\n\n";

		ss << "# HELP cpp_service_requests_successful Total successful HTTP requests\n";
		ss << "# TYPE cpp_service_requests_successful counter\n";
		ss << "cpp_service_requests_successful " << requests_successful_ << "\n\n";

		ss << "# HELP cpp_service_requests_failed Total failed HTTP requests\n";
		ss << "# TYPE cpp_service_requests_failed counter\n";
		ss << "cpp_service_requests_failed " << requests_failed_ << "\n\n";

		ss << "# HELP cpp_service_connections_total Total number of connections\n";
		ss << "# TYPE cpp_service_connections_total counter\n";
		ss << "cpp_service_connections_total " << connections_total_ << "\n\n";

		// Gauge metrics
		ss << "# HELP cpp_service_active_connections Current active connections\n";
		ss << "# TYPE cpp_service_active_connections gauge\n";
		ss << "cpp_service_active_connections " << active_connections_ << "\n\n";

		ss << "# HELP cpp_service_request_duration_seconds Last request duration in seconds\n";
		ss << "# TYPE cpp_service_request_duration_seconds gauge\n";
		ss << "cpp_service_request_duration_seconds " << request_duration_seconds_ << "\n\n";

		// Histogram metrics
		ss << "# HELP cpp_service_request_duration_seconds_histogram Request duration histogram\n";
		ss << "# TYPE cpp_service_request_duration_seconds_histogram histogram\n";
		ss << "cpp_service_request_duration_seconds_histogram_bucket{le=\"0.001\"} " << request_duration_bucket_1ms_ << "\n";
		ss << "cpp_service_request_duration_seconds_histogram_bucket{le=\"0.01\"} " << request_duration_bucket_10ms_ << "\n";
		ss << "cpp_service_request_duration_seconds_histogram_bucket{le=\"0.1\"} " << request_duration_bucket_100ms_ << "\n";
		ss << "cpp_service_request_duration_seconds_histogram_bucket{le=\"1.0\"} " << request_duration_bucket_1s_ << "\n";
		ss << "cpp_service_request_duration_seconds_histogram_bucket{le=\"+Inf\"} " << request_duration_bucket_inf_ << "\n";
		ss << "cpp_service_request_duration_seconds_histogram_sum " << request_duration_sum_ << "\n";
		ss << "cpp_service_request_duration_seconds_histogram_count " << request_duration_count_ << "\n\n";

		// Throughput metrics
		ss << "# HELP cpp_service_bytes_received_total Total bytes received\n";
		ss << "# TYPE cpp_service_bytes_received_total counter\n";
		ss << "cpp_service_bytes_received_total " << bytes_received_ << "\n\n";

		ss << "# HELP cpp_service_bytes_sent_total Total bytes sent\n";
		ss << "# TYPE cpp_service_bytes_sent_total counter\n";
		ss << "cpp_service_bytes_sent_total " << bytes_sent_ << "\n\n";

		// Server info metric
		ss << "# HELP cpp_service_info Server information\n";
		ss << "# TYPE cpp_service_info gauge\n";
		ss << "cpp_service_info{version=\"1.0.0\"} 1\n";

		return ss.str();
	}

private:
	Metrics() = default;

	// Request metrics
	std::atomic<long> requests_total_{0};
	std::atomic<long> requests_successful_{0};
	std::atomic<long> requests_failed_{0};

	// Connection metrics
	std::atomic<long> connections_total_{0};
	std::atomic<int> active_connections_{0};

	// Timing metrics
	std::atomic<double> request_duration_seconds_{0.0};
	std::mutex duration_mutex_;

	// Histogram metrics
	std::atomic<long> request_duration_bucket_1ms_{0};
	std::atomic<long> request_duration_bucket_10ms_{0};
	std::atomic<long> request_duration_bucket_100ms_{0};
	std::atomic<long> request_duration_bucket_1s_{0};
	std::atomic<long> request_duration_bucket_inf_{0};
	std::atomic<double> request_duration_sum_{0.0};
	std::atomic<long> request_duration_count_{0};
	std::mutex histogram_mutex_;

	// Throughput metrics
	std::atomic<long> bytes_received_{0};
	std::atomic<long> bytes_sent_{0};
};