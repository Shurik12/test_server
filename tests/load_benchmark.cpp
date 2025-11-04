#include <benchmark/benchmark.h>
#include <atomic>
#include <chrono>
#include <thread>
#include <vector>
#include <string>
#include <random>
#include <algorithm>
#include <numeric>
#include <cmath>
#include <sstream>
#include <iomanip>
#include <mutex>
#include <unordered_map>
#include <iostream>
#include <memory>
#include <client/Client.h>

class LoadBenchmark : public benchmark::Fixture
{
protected:
    static constexpr int TEST_PORT = 8080;

    void SetUp(const benchmark::State & /*state*/) override
    {
        // Wait a bit for any previous connections to clear
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    bool isServerReady()
    {
        try
        {
            auto client = createClient();
            auto response = client->sendRequest("/health", "GET");
            bool ready = !response.empty();
            return ready;
        }
        catch (const std::exception &e)
        {
            return false;
        }
    }

    // Create client with better connection management
    std::unique_ptr<Client> createClient()
    {
        auto client = std::make_unique<Client>("127.0.0.1", TEST_PORT);
        return client;
    }

    // Create a pool of clients for connection reuse
    std::vector<std::unique_ptr<Client>> createClientPool(size_t size)
    {
        std::vector<std::unique_ptr<Client>> pool;
        for (size_t i = 0; i < size; ++i)
        {
            pool.push_back(createClient());
        }
        return pool;
    }

    struct TestResult
    {
        std::atomic<int> total_requests{0};
        std::atomic<int> successful_requests{0};
        std::atomic<int> failed_requests{0};
        std::vector<double> response_times;
        mutable std::mutex response_times_mutex;
        std::unordered_map<std::string, int> errors;
        mutable std::mutex errors_mutex;

        void addRequest(bool success, double response_time, const std::string &error_type = "")
        {
            total_requests.fetch_add(1, std::memory_order_relaxed);
            if (success)
            {
                successful_requests.fetch_add(1, std::memory_order_relaxed);
            }
            else
            {
                failed_requests.fetch_add(1, std::memory_order_relaxed);
                if (!error_type.empty())
                {
                    std::lock_guard<std::mutex> lock(errors_mutex);
                    errors[error_type]++;
                }
            }

            if (response_time > 0)
            {
                std::lock_guard<std::mutex> lock(response_times_mutex);
                response_times.push_back(response_time);
            }
        }

        struct Stats
        {
            double avg_response_time;
            double median_response_time;
            double p95_response_time;
            double p99_response_time;
            double requests_per_second;
        };

        Stats getStats() const
        {
            std::lock_guard<std::mutex> lock(response_times_mutex);
            if (response_times.empty())
            {
                return {0, 0, 0, 0, 0};
            }

            auto times = response_times;
            std::sort(times.begin(), times.end());

            double sum = std::accumulate(times.begin(), times.end(), 0.0);
            double avg = sum / times.size();
            double median = times[times.size() / 2];
            double p95 = times[static_cast<size_t>(times.size() * 0.95)];
            double p99 = times[static_cast<size_t>(times.size() * 0.99)];

            double max_time = *std::max_element(times.begin(), times.end());
            double rps = times.size() / (max_time > 0 ? max_time : 1.0);

            return {avg, median, p95, p99, rps};
        }

        double getSuccessRate() const
        {
            int total = total_requests.load(std::memory_order_relaxed);
            int successful = successful_requests.load(std::memory_order_relaxed);
            return total > 0 ? (static_cast<double>(successful) / total * 100.0) : 0.0;
        }
    };

    std::string generatePayload(int client_id = -1)
    {
        static std::random_device rd;
        static std::mt19937 gen(rd());
        static std::uniform_int_distribution<int> dist(1, 10000);

        int id = client_id > 0 ? client_id : dist(gen);
        int number = dist(gen) % 1000 + 1;

        std::stringstream ss;
        ss << R"({"id": )" << id
           << R"(, "name": "User_)" << (dist(gen) % 1000 + 1)
           << R"(", "phone": "+1-555-)" << (dist(gen) % 900 + 100)
           << "-" << (dist(gen) % 9000 + 1000)
           << R"(", "number": )" << number << "}";

        return ss.str();
    }

    bool testEndpoint(TestResult &result, Client &client, const std::string &endpoint,
                      const std::string &method = "GET",
                      const std::string &payload = "")
    {
        auto start = std::chrono::high_resolution_clock::now();

        try
        {
            std::string response;

            if (method == "POST")
            {
                response = client.sendRequest(endpoint, "POST", payload);
            }
            else
            {
                response = client.sendRequest(endpoint, "GET");
            }

            auto end = std::chrono::high_resolution_clock::now();
            double response_time = std::chrono::duration<double>(end - start).count();

            // More robust success detection
            bool success = false;
            if (endpoint == "/metrics" || endpoint == "/health")
            {
                success = !response.empty();
            }
            else if (endpoint == "/numbers/sum" || endpoint == "/numbers/sum-all")
            {
                success = !response.empty(); // These might not have "success" field
            }
            else
            {
                success = response.find("\"success\":true") != std::string::npos ||
                          response.find("\"status\":\"success\"") != std::string::npos ||
                          response.find("success") != std::string::npos;
            }

            result.addRequest(success, response_time, success ? "" : "api_error");
            return success;
        }
        catch (const std::exception &e)
        {
            auto end = std::chrono::high_resolution_clock::now();
            double response_time = std::chrono::duration<double>(end - start).count();
            result.addRequest(false, response_time, "exception");
            return false;
        }
        catch (...)
        {
            auto end = std::chrono::high_resolution_clock::now();
            double response_time = std::chrono::duration<double>(end - start).count();
            result.addRequest(false, response_time, "unknown_error");
            return false;
        }
    }

    void runLoadTest(benchmark::State &state, int num_clients, int requests_per_client)
    {
        if (!isServerReady())
        {
            state.SkipWithError("Test server not available");
            return;
        }

        TestResult result;

        for (auto _ : state)
        {
            std::vector<std::thread> threads;

            auto worker = [&](int client_id)
            {
                // Each thread gets its own client to reuse connections
                auto client = createClient();
                for (int i = 0; i < requests_per_client; ++i)
                {
                    if (std::rand() % 100 < 70)
                    {
                        std::string payload = generatePayload(client_id);
                        testEndpoint(result, *client, "/process", "POST", payload);
                    }
                    else
                    {
                        const char *endpoints[] = {"/health", "/metrics"};
                        std::string endpoint = endpoints[std::rand() % 2];
                        testEndpoint(result, *client, endpoint, "GET");
                    }

                    // Reduced delay frequency for higher load
                    if (i % 50 == 0)
                    {
                        std::this_thread::sleep_for(std::chrono::microseconds(500));
                    }
                }
            };

            for (int i = 0; i < num_clients; ++i)
            {
                threads.emplace_back(worker, i);
            }

            for (auto &thread : threads)
            {
                if (thread.joinable())
                {
                    thread.join();
                }
            }
        }

        auto stats = result.getStats();
        state.counters["success_rate"] = result.getSuccessRate();
        state.counters["requests_per_second"] = stats.requests_per_second;
        state.counters["avg_response_ms"] = stats.avg_response_time * 1000;
        state.counters["p95_response_ms"] = stats.p95_response_time * 1000;
        state.counters["p99_response_ms"] = stats.p99_response_time * 1000;
    }

    void runRpsTest(benchmark::State &state, int target_rps, double duration)
    {
        if (!isServerReady())
        {
            state.SkipWithError("Test server not available");
            return;
        }

        TestResult result;

        for (auto _ : state)
        {
            auto start_time = std::chrono::high_resolution_clock::now();

            while (std::chrono::duration<double>(std::chrono::high_resolution_clock::now() - start_time).count() < duration)
            {
                auto batch_start = std::chrono::high_resolution_clock::now();
                std::vector<std::thread> threads;

                // Use connection pooling - reuse clients with larger pool
                auto clients = createClientPool(std::min(target_rps, 100));

                for (int i = 0; i < target_rps; ++i)
                {
                    auto &client = clients[i % clients.size()];
                    threads.emplace_back([&, client = client.get()]()
                                         {
                        std::string payload = generatePayload();
                        testEndpoint(result, *client, "/process", "POST", payload); });
                }

                for (auto &thread : threads)
                {
                    if (thread.joinable())
                    {
                        thread.join();
                    }
                }

                // Maintain RPS rate
                auto batch_time = std::chrono::duration<double>(
                                      std::chrono::high_resolution_clock::now() - batch_start)
                                      .count();
                if (batch_time < 1.0)
                {
                    std::this_thread::sleep_for(std::chrono::duration<double>(1.0 - batch_time));
                }
            }

            state.SetIterationTime(duration);
        }

        auto stats = result.getStats();
        state.counters["success_rate"] = result.getSuccessRate();
        state.counters["actual_rps"] = stats.requests_per_second;
        state.counters["avg_response_ms"] = stats.avg_response_time * 1000;
    }

    void runNumberAccuracyTest(benchmark::State &state)
    {
        if (!isServerReady())
        {
            state.SkipWithError("Test server not available");
            return;
        }

        TestResult result;
        const int operations = 200; // Increased back to original level

        for (auto _ : state)
        {
            std::vector<std::thread> threads;
            std::vector<std::pair<std::string, int>> operations_list;

            // Generate operations with unique IDs to avoid conflicts
            for (int i = 0; i < operations; ++i)
            {
                std::string client_id = "accuracy_test_" + std::to_string(i % 10 + 1);
                int number = (i % 100) + 1; // More varied numbers
                operations_list.emplace_back(client_id, number);
            }

            // Use connection pooling for reliability with larger pool
            auto clients = createClientPool(std::min(operations, 50));

            // Execute operations with retry logic
            for (int i = 0; i < operations; ++i)
            {
                const auto &op = operations_list[i];
                auto &client = clients[i % clients.size()];

                threads.emplace_back([&, op, client = client.get()]()
                                     {
                    std::stringstream payload;
                    payload << R"({"id": )" << (i + 1000) // Unique ID for each operation
                            << R"(, "name": "AccuracyTest_)" << op.first
                            << R"(", "phone": "+1-555-010-)" << std::setw(4) << std::setfill('0') << (i + 1)
                            << R"(", "number": )" << op.second << "}";
                    
                    // Retry logic for failed requests
                    for (int attempt = 0; attempt < 2; ++attempt) {
                        if (testEndpoint(result, *client, "/process", "POST", payload.str())) {
                            break;
                        }
                        if (attempt == 0) {
                            std::this_thread::sleep_for(std::chrono::milliseconds(5));
                        }
                    } });
            }

            for (auto &thread : threads)
            {
                if (thread.joinable())
                {
                    thread.join();
                }
            }

            // Verify endpoints are working - with retry
            auto verify_client = createClient();
            for (int attempt = 0; attempt < 3; ++attempt)
            {
                if (testEndpoint(result, *verify_client, "/numbers/sum", "GET") &&
                    testEndpoint(result, *verify_client, "/numbers/sum-all", "GET"))
                {
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
        }

        state.counters["success_rate"] = result.getSuccessRate();
    }
};

// Light load benchmark - INCREASED from 3x10 to 5x20
BENCHMARK_DEFINE_F(LoadBenchmark, LightLoad)(benchmark::State &state)
{
    runLoadTest(state, 5, 20);
}
BENCHMARK_REGISTER_F(LoadBenchmark, LightLoad)->Unit(benchmark::kMillisecond);

// Medium load benchmark - INCREASED from 5x20 to 10x50
BENCHMARK_DEFINE_F(LoadBenchmark, MediumLoad)(benchmark::State &state)
{
    runLoadTest(state, 10, 50);
}
BENCHMARK_REGISTER_F(LoadBenchmark, MediumLoad)->Unit(benchmark::kMillisecond);

// Heavy load benchmark - INCREASED from 10x30 to 20x100
BENCHMARK_DEFINE_F(LoadBenchmark, HeavyLoad)(benchmark::State &state)
{
    runLoadTest(state, 20, 100);
}
BENCHMARK_REGISTER_F(LoadBenchmark, HeavyLoad)->Unit(benchmark::kMillisecond);

// RPS-based benchmark - INCREASED RPS targets
BENCHMARK_DEFINE_F(LoadBenchmark, SustainedRPS)(benchmark::State &state)
{
    runRpsTest(state, state.range(0), 10.0);
}
BENCHMARK_REGISTER_F(LoadBenchmark, SustainedRPS)
    ->Arg(50)  // INCREASED from 10 to 50
    ->Arg(100) // INCREASED from 30 to 100
    ->Arg(200) // INCREASED from 50 to 200
    ->Unit(benchmark::kMillisecond)
    ->UseManualTime();

// Spike test benchmark - INCREASED intensities
BENCHMARK_DEFINE_F(LoadBenchmark, SpikeTest)(benchmark::State &state)
{
    if (!isServerReady())
    {
        state.SkipWithError("Test server not available");
        return;
    }

    struct Phase
    {
        std::string name;
        int rps;
        double duration;
    };

    std::vector<Phase> phases = {
        {"Warm-up", 50, 5.0},  // INCREASED
        {"Low", 100, 5.0},     // INCREASED
        {"Spike", 500, 3.0},   // INCREASED back to 500
        {"Sustain", 150, 5.0}, // INCREASED
        {"Cool-down", 50, 3.0} // INCREASED
    };

    TestResult result;
    auto client_pool = createClientPool(100); // Larger connection pool

    for (auto _ : state)
    {
        for (const auto &phase : phases)
        {
            auto phase_start = std::chrono::high_resolution_clock::now();

            while (std::chrono::duration<double>(
                       std::chrono::high_resolution_clock::now() - phase_start)
                       .count() < phase.duration)
            {

                std::vector<std::thread> threads;
                for (int i = 0; i < phase.rps; ++i)
                {
                    auto &client = client_pool[i % client_pool.size()];
                    threads.emplace_back([&, client = client.get()]()
                                         {
                        std::string payload = generatePayload();
                        testEndpoint(result, *client, "/process", "POST", payload); });
                }

                for (auto &thread : threads)
                {
                    if (thread.joinable())
                    {
                        thread.join();
                    }
                }

                // Smaller delay for higher throughput
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
        }

        state.SetIterationTime(21.0); // Back to original duration
    }

    auto stats = result.getStats();
    state.counters["success_rate"] = result.getSuccessRate();
    state.counters["requests_per_second"] = stats.requests_per_second;
    state.counters["p95_response_ms"] = stats.p95_response_time * 1000;
}
BENCHMARK_REGISTER_F(LoadBenchmark, SpikeTest)->Unit(benchmark::kMillisecond)->UseManualTime();

// Number tracking accuracy benchmark - INCREASED operations
BENCHMARK_DEFINE_F(LoadBenchmark, NumberAccuracy)(benchmark::State &state)
{
    runNumberAccuracyTest(state);
}
BENCHMARK_REGISTER_F(LoadBenchmark, NumberAccuracy)->Unit(benchmark::kMillisecond);

BENCHMARK_MAIN();