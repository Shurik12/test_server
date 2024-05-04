#include <execution>
#define SEQ std::execution::seq
#define PAR std::execution::par

#include <Server/NewConnection.h>
#include <Server/Worker.h>
#include <iostream>
#include <vector>

using namespace std;

void NewConnection::run()
{
	Poco::Net::StreamSocket & ss = socket();
	string message;

  	try 
	{
		char buffer[256];
		int n {ss.receiveBytes(buffer, sizeof(buffer))};
		int s {0};

  	  	cout << "Received: " << string(buffer, n) << "\n";

		while (n > 0) 
	  	{
	  		s = 0;
	  		const vector<int> v1(n/2, rand());
	  		const vector<int> v2(n/2, rand());

	  		// my check
	  		// const vector<int> v1 {1, 2, 3};
	  		// const vector<int> v2 {4, 5, 6};

	  		s += sum(v1);
	  		s += sum(v2);
	  		// executeOnWorkers(v1, v2);

			message = to_string(s);
			ss.sendBytes(message.data(), message.size());
  	  	  	n = ss.receiveBytes(buffer, sizeof(buffer));
  	  	  	cout << "Received: " 
					  << string(buffer, n) << "\n";
  	  	}
  	} 
	catch (Poco::Exception &exc) 
	{
		cerr << "NewConnection: " << exc.displayText() << "\n";
  	}
}

int NewConnection::sum(const vector<int> & v)
{
  	return reduce(execution::par_unseq, v.begin(), v.end());
}

void NewConnection::executeOnWorkers(const vector<int> & v1, const vector<int> & v2)
{
	int sum {0};

	// Create calculate workers
	Worker work1(1);
	Worker work2(2);
		
	Poco::ThreadPool threadpool;

	threadpool.start(work1);
	threadpool.start(work2);
	
	threadpool.joinAll();

	sum += work1.sum1(v1);
	sum += work2.sum1(v2);

	threadpool.joinAll();
	
	cout << sum << "\n";
}