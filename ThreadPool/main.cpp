#include "threadpool.h"

#define ull unsigned long long

class MyTask : public Task
{
public:
	Any run() override {
		std::cout << "tid: " << std::this_thread::get_id() << ", begin!" << std::endl;
		ull sum = 0;
		for (int i = begin_; i < end_; ++i)
		{
			sum += i;
		}
		std::this_thread::sleep_for(std::chrono::seconds(3));
		std::cout << "tid: " << std::this_thread::get_id() << ", end!" << std::endl;

		return sum;
	}
private:
	int begin_;
	int end_;

public:
	MyTask(): begin_(0), end_(0)
	{}

	MyTask(int begin, int end): begin_(begin), end_(end) 
	{}
};

int main()
{
	{
		ThreadPool pool;
		pool.setMode(PoolMode::CACHED_MODE);
		pool.start(4);

		Result result = pool.submitTask(std::make_shared<MyTask>(1, 1000000000));
		pool.submitTask(std::make_shared<MyTask>(1, 1000000000));
		pool.submitTask(std::make_shared<MyTask>(1, 1000000000));
		pool.submitTask(std::make_shared<MyTask>(1, 1000000000));
		pool.submitTask(std::make_shared<MyTask>(1, 1000000000));
		//pool.submitTask(std::make_shared<MyTask>(1, 1000000000));
		//pool.submitTask(std::make_shared<MyTask>(1, 1000000000));
		//pool.submitTask(std::make_shared<MyTask>(1, 1000000000));
		//pool.submitTask(std::make_shared<MyTask>(1, 1000000000));


		ull sum = result.get().cast_<ull>();
		std::cout << sum << std::endl;

	}
	std::cout << "main over!" << std::endl;
	getchar();
	return 0;
}