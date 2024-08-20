#include "threadpool.h"

const int TASK_MAX_THRESHOLD = 1024;
const int THREAD_SIZE_THRESHOLD = 100;
const int THREAD_MAX_IDLE_TIME = 5; //  单位：秒

// 构造函数
ThreadPool::ThreadPool()
	:initThreadSize_(0),
	curThreadSize_(0),
	idleThreadSize_(0),
	threadSizeThreshold_(THREAD_SIZE_THRESHOLD),
	taskSize_(0),
	taskQueMaxThreshold_(TASK_MAX_THRESHOLD),
	poolMode_(PoolMode::FIXED_MODE),
	isPoolRunning_(false)
{
}

// 析构函数
ThreadPool::~ThreadPool()
{
	isPoolRunning_ = false;
	// 等待线程池里面所有线程返回 有两种状态：阻塞 + 正在运行
	std::unique_lock<std::mutex> lock(taskMutex_);
	notEmpty_.notify_all();		// 阻塞 + 运行
	exitCond_.wait(lock, [&]()->bool {return threads_.size() == 0; });
}

// 设置线程池的工作模式
void ThreadPool::setMode(PoolMode mode)
{
	if (checkPoolStatus())
		return;
	this->poolMode_ = mode;
}

bool ThreadPool::checkPoolStatus() const
{
	return isPoolRunning_;
}

// 开启线程池
void ThreadPool::start(int initThreadSize)
{
	isPoolRunning_ = true;
	this->initThreadSize_ = initThreadSize;

	// 根据初始数量的线程个数，创建线程，放入线程池的线程列表中
	for (int i = 0; i < initThreadSize_; ++i)
	{
		auto thread = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
		int threadID = thread->getID();
		threads_.emplace(threadID, std::move(thread));
	}

	// 依次启动线程
	for (int i = 0; i < initThreadSize_; ++i)
	{
		threads_[i]->start();
		curThreadSize_++;		// 当前线程个数++
		idleThreadSize_++;		// 空闲线程个数++
	}

}

// 给线程池的任务队列提交任务,相当于生产者
Result ThreadPool::submitTask(std::shared_ptr<Task> sp)
{
	// 获取任务队列锁
	std::unique_lock<std::mutex> lock(taskMutex_);

	// 线程的通信，等待notFull_
	if (!notFull_.wait_for(lock, std::chrono::seconds(1), [&]()->bool {
		return taskQue_.size() < taskQueMaxThreshold_;
		}))
	{
		// notFull_等待1s中，线程队列还是满的
		std::cerr << "task queue is full. submit task failed." << std::endl;
		return Result(sp, false);
	}
	
	// 有空余，将任务放入任务队列中,任务队列中的任务数+1
	taskQue_.emplace(sp);
	taskSize_++;

	// 任务队列不空，通知消费者进行消费,此时notEmpty_
	notEmpty_.notify_all();

	// cache 模式 需要根据任务数量以及空闲线程的数量，动态创建线程
	if (poolMode_ == PoolMode::CACHED_MODE &&
		taskSize_ > idleThreadSize_ &&
		curThreadSize_ < threadSizeThreshold_)
	{
		std::cerr << "create new thread..." << std::endl;
		// 创建线程
		auto thread = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
		int id = thread->getID();
		threads_.emplace(id, std::move(thread));
		threads_[id]->start();	// 启动线程
		curThreadSize_++;		// 当前线程个数++
		idleThreadSize_++;		// 空闲线程个数++
	}

	// 返回任务执行的结果
	return Result(sp);

}

// 设置线程池中任务队列的最大任务数
void ThreadPool::setTaskQueMaxThreshold(size_t taskQueMaxThreshold)
{
	if (checkPoolStatus())
		return;
	this->taskQueMaxThreshold_ = taskQueMaxThreshold;
}

// cache模式下，用户可自定义线程数量
void ThreadPool::setThreadMaxThreashold(size_t threadMaxThreshold)
{
	if (checkPoolStatus())
		return;

	if (poolMode_ == PoolMode::CACHED_MODE)
	{
		this->threadSizeThreshold_ = threadMaxThreshold;
	}
}



// 定义线程函数,线程池中的线程从任务队列中消费任务,相当于消费者线程
void ThreadPool::threadFunc(int threadID)
{
	// 获取当前线程第一次执行时间
	auto lastTime = std::chrono::high_resolution_clock().now();
	while (true)
	{
		std::shared_ptr<Task> task;
		// 先获取锁
		std::unique_lock<std::mutex> lock(taskMutex_);
		std::cout << "tid: " << std::this_thread::get_id() << "，尝试获取任务" << std::endl;

		while (taskQue_.size() == 0)
		{
			// 没任务了，线程池也不工作了
			if (!isPoolRunning_)
			{
				threads_.erase(threadID);
				std::cout << "tid: " << std::this_thread::get_id() << " exit!" << std::endl;
				exitCond_.notify_all();
				return;
			}

			// cached mode 可能创建了很多线程，超过60s，需要回收线程
			if (poolMode_ == PoolMode::CACHED_MODE)
			{
				// 条件变量超时返回
				if (std::cv_status::timeout == notEmpty_.wait_for(lock, std::chrono::seconds(1)))
				{
					// 计算当前时间
					auto now = std::chrono::high_resolution_clock().now();
					// 计算间隔时间
					auto duration = std::chrono::duration_cast<std::chrono::seconds>(now - lastTime);
					if (duration.count() >= THREAD_MAX_IDLE_TIME && curThreadSize_ > initThreadSize_)
					{
						// 超过最大时间，回收线程，释放资源
						threads_.erase(threadID);
						curThreadSize_--;
						idleThreadSize_--;
						std::cout << "threadID: " << std::this_thread::get_id() << " exit!" << std::endl;
						return;		// 线程函数结束，线程自然也就结束了
					}
				}
			}
			else
			{
				// 等待notEmpty_
				notEmpty_.wait(lock);
			}
		}
		// 有任务就继续执行
		idleThreadSize_--;		// 线程执行，空闲线程--
		std::cout << "tid: " << std::this_thread::get_id() << "，获取任务成功" << std::endl;

		// 从任务队列中取一个任务出来
		task = taskQue_.front();
		taskQue_.pop();
		taskSize_--;

		// 任务队列仍然不空，通知其他消费者
		if (taskQue_.size() > 0)
		{
			notEmpty_.notify_all();
		}

		// 通知生产者可以往任务队列中添加任务了
		notFull_.notify_all();		
		
		if (task != nullptr ) 
			task->exec();  // 当前线程负责执行这个任务
		idleThreadSize_++;	// 任务执行完，空闲线程个数++
		lastTime = std::chrono::high_resolution_clock().now();  // 更新最后一个执行时间
	}
}

///////////////////////// Task相关

Task::Task()
	: result_(nullptr)
{

}

void Task::exec()
{
	if (result_ != nullptr)
	{
		result_->setVal(run());		// 这里发生多态调用
	}
}

void Task::setResult(Result* result)
{
	result_ = result;
}


///////////////////////// 线程类相关

int Thread::generateID_ = 0;

Thread::Thread(Func func)
	: func_(func)
{
	threadID_ = generateID_++;
}

Thread::~Thread()
{

}

// 线程启动函数
void Thread::start()
{
	std::thread t(func_, threadID_);	// 开启一个线程执行线程函数
	t.detach();							// 分离线程
}


//////////////// Result

Result::Result(std::shared_ptr<Task> task, bool isValid) 
	:task_(task), isValid_(isValid)
{
	task_->setResult(this);
}

Any Result::get()
{
	if (!isValid_)
	{
		return "";
	}

	sem_.wait();	// 等待任务线程执行结束，阻塞用户线程
	return std::move(any_);
}

void Result::setVal(Any any)
{
	this->any_ = std::move(any);
	sem_.post();   // 增加资源
}
