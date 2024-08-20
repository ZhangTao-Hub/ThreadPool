#ifndef THREADPOOL_H__
#define THREADPOOL_H__

#include <vector>
#include <unordered_map>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <atomic>
#include <memory>
#include <functional>
#include <thread>
#include <chrono>
#include <iostream>

// Any类型：定义接受任意数据的类型
class Any
{
public:
	Any() = default;
	~Any() = default;

	/*
	*	Any类中的成员变量base_是unique_ptr,因此需要禁用Any类对象进行左值引用的拷贝和赋值操作
	*/
	Any(const Any&) = delete;
	Any& operator=(const Any&) = delete;
	Any(Any&&) = default;
	Any& operator=(Any&&) = default;

	// 该构造函数让Any类接受任意类型的数据
	template<typename T>
	Any(T data)
		: base_(std::make_unique<Derive<T>>(data)) 
	{}

	// 该方法将Any类对象中存储的data数据取出来
	template<typename T>
	T cast_()
	{
		// 从base_中取出派生类指针，并返回派生类对象成员变量
		// 基类指针转派生类指针 RTTI
		Derive<T>* pd = dynamic_cast<Derive<T>*>(base_.get());
		if (pd == nullptr)
		{
			throw "type is unmatch!";
		}
		return pd->data_;
	}

private:
	// 基类
	class Base
	{
	public:
		virtual ~Base() = default;
	};

	// 派生类
	template<typename T>
	class Derive : public Base
	{
	public:
		Derive() 
		{}
		Derive(T data) : data_(data) 
		{}
		T data_;		// 存储任意类型
	};
private:
	// 定义一个基类指针，用于指向派生类对象
	std::unique_ptr<Base> base_;
};

// 实现一个信号量类 用于执行任务线程与调度任务返回值线程之间的通信
class Semaphore
{
private:
	int resource_;
	std::mutex mutex_;
	std::condition_variable cv;

public:
	Semaphore(int resource = 0)
		: resource_(resource) 
	{}

	~Semaphore() = default;

	// 获取一个信号量
	void wait()
	{
		std::unique_lock<std::mutex> lock(mutex_);
		cv.wait(lock, [&]()->bool
			{
				return resource_ > 0;
			});
		resource_--;
	}

	// 产生一个信号量
	void post()
	{
		std::unique_lock<std::mutex> lock(mutex_);
		resource_++;
		cv.notify_all();
	}
};

// task声明前置
class Task;

// 用于接受用户提交到线程池执行任务所得到的返回值
class Result
{
public:
	Result() = default;
	Result(std::shared_ptr<Task> task, bool isValid = true);

	Any get();						// 调用此方法获得任务的返回值
	void setVal(Any);				// 获取任务执行完的返回值

private:
	Any any_;						// 返回值
	Semaphore sem_;					// 线程通信信号量
	std::shared_ptr<Task> task_;	// 指向对应获取返回值的任务对象
	std::atomic_bool isValid_;		// 返回值是否有效
};

// 抽象任务基类
class Task
{
public:
	Task();
	~Task() = default;

	void setResult(Result*);		// 每一个task都有一个result
	void exec();					// 将用户重写的run方法包装，方便Result对象指针调用
	virtual Any run() = 0;			// 用户继承Task基类，自定义任务类型，重写run()方法

private:
	Result* result_;				// 不能使用强智能指针
};

// 线程类
class Thread
{
private:
	using Func = std::function<void(int)>;						// 定义线程函数类型
	Func func_;

	size_t threadID_;
	static int generateID_;										// 全局静态
public:
	Thread(Func);
	~Thread();

	size_t getID() const { return threadID_; }					// 获取当前线程id
	void start();												// 启动线程
};

// 线程池的两种模式
enum class PoolMode
{
	FIXED_MODE,													// 固定线程个数
	CACHED_MODE,												// 动态扩充线程个数
};

// 线程池类
class ThreadPool
{
private:
	//std::vector<std::unique_ptr<Thread>> threads_;			// 线程列表
	std::unordered_map<size_t, std::unique_ptr<Thread>> threads_;// 线程列表
	size_t initThreadSize_;										// 初始线程个数
	std::atomic_int curThreadSize_;								// 当前线程数量
	std::atomic_int idleThreadSize_;							// 空闲线程的数量
	size_t threadSizeThreshold_;								// 线程数量最大值

	std::queue<std::shared_ptr<Task>> taskQue_;					// 任务队列
	size_t taskQueMaxThreshold_;								// 任务队列的最大个数
	std::atomic_uint taskSize_;									// 任务个数

	std::mutex taskMutex_;										// 保证任务队列线程安全
	std::condition_variable notFull_;							// 任务队列条件变量非满
	std::condition_variable notEmpty_;							// 任务队列条件变量非空
	std::condition_variable exitCond_;							// 线程池结束的条件变量

	PoolMode poolMode_;											// 线程池的工作方式
	std::atomic_bool isPoolRunning_;							// 判断线程池是否已经开始工作


private:
	void threadFunc(int);										// 线程函数，线程启动执行该函数
	bool checkPoolStatus() const;								// 检查当前线程池的状态

public:
	ThreadPool();
	~ThreadPool();
	ThreadPool(const ThreadPool&) = delete;						// 禁用拷贝构造
	ThreadPool& operator=(const ThreadPool&) = delete;			// 禁用赋值运算符

	void setMode(PoolMode mode);								// 设置线程池的工作模式
	void start(int initThreadSize = std::thread::hardware_concurrency());							// 开启线程池
	Result submitTask(std::shared_ptr<Task>);					// 给线程池提交任务
	void setTaskQueMaxThreshold(size_t);						// 设置线程池最大任务数
	void setThreadMaxThreashold(size_t);						// 设置线程池最大线程数量
	
};

#endif // !THREADPOOL_H__


