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

// Any���ͣ���������������ݵ�����
class Any
{
public:
	Any() = default;
	~Any() = default;

	/*
	*	Any���еĳ�Ա����base_��unique_ptr,�����Ҫ����Any����������ֵ���õĿ����͸�ֵ����
	*/
	Any(const Any&) = delete;
	Any& operator=(const Any&) = delete;
	Any(Any&&) = default;
	Any& operator=(Any&&) = default;

	// �ù��캯����Any������������͵�����
	template<typename T>
	Any(T data)
		: base_(std::make_unique<Derive<T>>(data)) 
	{}

	// �÷�����Any������д洢��data����ȡ����
	template<typename T>
	T cast_()
	{
		// ��base_��ȡ��������ָ�룬����������������Ա����
		// ����ָ��ת������ָ�� RTTI
		Derive<T>* pd = dynamic_cast<Derive<T>*>(base_.get());
		if (pd == nullptr)
		{
			throw "type is unmatch!";
		}
		return pd->data_;
	}

private:
	// ����
	class Base
	{
	public:
		virtual ~Base() = default;
	};

	// ������
	template<typename T>
	class Derive : public Base
	{
	public:
		Derive() 
		{}
		Derive(T data) : data_(data) 
		{}
		T data_;		// �洢��������
	};
private:
	// ����һ������ָ�룬����ָ�����������
	std::unique_ptr<Base> base_;
};

// ʵ��һ���ź����� ����ִ�������߳���������񷵻�ֵ�߳�֮���ͨ��
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

	// ��ȡһ���ź���
	void wait()
	{
		std::unique_lock<std::mutex> lock(mutex_);
		cv.wait(lock, [&]()->bool
			{
				return resource_ > 0;
			});
		resource_--;
	}

	// ����һ���ź���
	void post()
	{
		std::unique_lock<std::mutex> lock(mutex_);
		resource_++;
		cv.notify_all();
	}
};

// task����ǰ��
class Task;

// ���ڽ����û��ύ���̳߳�ִ���������õ��ķ���ֵ
class Result
{
public:
	Result() = default;
	Result(std::shared_ptr<Task> task, bool isValid = true);

	Any get();						// ���ô˷����������ķ���ֵ
	void setVal(Any);				// ��ȡ����ִ����ķ���ֵ

private:
	Any any_;						// ����ֵ
	Semaphore sem_;					// �߳�ͨ���ź���
	std::shared_ptr<Task> task_;	// ָ���Ӧ��ȡ����ֵ���������
	std::atomic_bool isValid_;		// ����ֵ�Ƿ���Ч
};

// �����������
class Task
{
public:
	Task();
	~Task() = default;

	void setResult(Result*);		// ÿһ��task����һ��result
	void exec();					// ���û���д��run������װ������Result����ָ�����
	virtual Any run() = 0;			// �û��̳�Task���࣬�Զ����������ͣ���дrun()����

private:
	Result* result_;				// ����ʹ��ǿ����ָ��
};

// �߳���
class Thread
{
private:
	using Func = std::function<void(int)>;						// �����̺߳�������
	Func func_;

	size_t threadID_;
	static int generateID_;										// ȫ�־�̬
public:
	Thread(Func);
	~Thread();

	size_t getID() const { return threadID_; }					// ��ȡ��ǰ�߳�id
	void start();												// �����߳�
};

// �̳߳ص�����ģʽ
enum class PoolMode
{
	FIXED_MODE,													// �̶��̸߳���
	CACHED_MODE,												// ��̬�����̸߳���
};

// �̳߳���
class ThreadPool
{
private:
	//std::vector<std::unique_ptr<Thread>> threads_;			// �߳��б�
	std::unordered_map<size_t, std::unique_ptr<Thread>> threads_;// �߳��б�
	size_t initThreadSize_;										// ��ʼ�̸߳���
	std::atomic_int curThreadSize_;								// ��ǰ�߳�����
	std::atomic_int idleThreadSize_;							// �����̵߳�����
	size_t threadSizeThreshold_;								// �߳��������ֵ

	std::queue<std::shared_ptr<Task>> taskQue_;					// �������
	size_t taskQueMaxThreshold_;								// ������е�������
	std::atomic_uint taskSize_;									// �������

	std::mutex taskMutex_;										// ��֤��������̰߳�ȫ
	std::condition_variable notFull_;							// �������������������
	std::condition_variable notEmpty_;							// ����������������ǿ�
	std::condition_variable exitCond_;							// �̳߳ؽ�������������

	PoolMode poolMode_;											// �̳߳صĹ�����ʽ
	std::atomic_bool isPoolRunning_;							// �ж��̳߳��Ƿ��Ѿ���ʼ����


private:
	void threadFunc(int);										// �̺߳������߳�����ִ�иú���
	bool checkPoolStatus() const;								// ��鵱ǰ�̳߳ص�״̬

public:
	ThreadPool();
	~ThreadPool();
	ThreadPool(const ThreadPool&) = delete;						// ���ÿ�������
	ThreadPool& operator=(const ThreadPool&) = delete;			// ���ø�ֵ�����

	void setMode(PoolMode mode);								// �����̳߳صĹ���ģʽ
	void start(int initThreadSize = std::thread::hardware_concurrency());							// �����̳߳�
	Result submitTask(std::shared_ptr<Task>);					// ���̳߳��ύ����
	void setTaskQueMaxThreshold(size_t);						// �����̳߳����������
	void setThreadMaxThreashold(size_t);						// �����̳߳�����߳�����
	
};

#endif // !THREADPOOL_H__


