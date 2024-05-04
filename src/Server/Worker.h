#include "Poco/Runnable.h"
#include "Poco/ThreadPool.h"
#include <iostream>
#include <vector>

#include <execution>
#define PAR_UNSEQ std::execution::par_unseq

class Worker : public Poco::Runnable
{
public:
	Worker(int id_) : id(id_) {}
	virtual void run()
	{
		std::cout << "i'm worker:" << id << "\n";
	}
	int sum1(const std::vector<int> & v)
	{
		return reduce(PAR_UNSEQ, v.begin(), v.end());
	}

private:
	int id;		
};
