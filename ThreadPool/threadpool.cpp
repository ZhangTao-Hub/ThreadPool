#include "threadpool.h"

const int TASK_MAX_THRESHOLD = 1024;
const int THREAD_SIZE_THRESHOLD = 100;
const int THREAD_MAX_IDLE_TIME = 5; //  ��λ����

// ���캯��
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

// ��������
ThreadPool::~ThreadPool()
{
	isPoolRunning_ = false;
	// �ȴ��̳߳����������̷߳��� ������״̬������ + ��������
	std::unique_lock<std::mutex> lock(taskMutex_);
	notEmpty_.notify_all();		// ���� + ����
	exitCond_.wait(lock, [&]()->bool {return threads_.size() == 0; });
}

// �����̳߳صĹ���ģʽ
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

// �����̳߳�
void ThreadPool::start(int initThreadSize)
{
	isPoolRunning_ = true;
	this->initThreadSize_ = initThreadSize;

	// ���ݳ�ʼ�������̸߳����������̣߳������̳߳ص��߳��б���
	for (int i = 0; i < initThreadSize_; ++i)
	{
		auto thread = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
		int threadID = thread->getID();
		threads_.emplace(threadID, std::move(thread));
	}

	// ���������߳�
	for (int i = 0; i < initThreadSize_; ++i)
	{
		threads_[i]->start();
		curThreadSize_++;		// ��ǰ�̸߳���++
		idleThreadSize_++;		// �����̸߳���++
	}

}

// ���̳߳ص���������ύ����,�൱��������
Result ThreadPool::submitTask(std::shared_ptr<Task> sp)
{
	// ��ȡ���������
	std::unique_lock<std::mutex> lock(taskMutex_);

	// �̵߳�ͨ�ţ��ȴ�notFull_
	if (!notFull_.wait_for(lock, std::chrono::seconds(1), [&]()->bool {
		return taskQue_.size() < taskQueMaxThreshold_;
		}))
	{
		// notFull_�ȴ�1s�У��̶߳��л�������
		std::cerr << "task queue is full. submit task failed." << std::endl;
		return Result(sp, false);
	}
	
	// �п��࣬������������������,��������е�������+1
	taskQue_.emplace(sp);
	taskSize_++;

	// ������в��գ�֪ͨ�����߽�������,��ʱnotEmpty_
	notEmpty_.notify_all();

	// cache ģʽ ��Ҫ�������������Լ������̵߳���������̬�����߳�
	if (poolMode_ == PoolMode::CACHED_MODE &&
		taskSize_ > idleThreadSize_ &&
		curThreadSize_ < threadSizeThreshold_)
	{
		std::cerr << "create new thread..." << std::endl;
		// �����߳�
		auto thread = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
		int id = thread->getID();
		threads_.emplace(id, std::move(thread));
		threads_[id]->start();	// �����߳�
		curThreadSize_++;		// ��ǰ�̸߳���++
		idleThreadSize_++;		// �����̸߳���++
	}

	// ��������ִ�еĽ��
	return Result(sp);

}

// �����̳߳���������е����������
void ThreadPool::setTaskQueMaxThreshold(size_t taskQueMaxThreshold)
{
	if (checkPoolStatus())
		return;
	this->taskQueMaxThreshold_ = taskQueMaxThreshold;
}

// cacheģʽ�£��û����Զ����߳�����
void ThreadPool::setThreadMaxThreashold(size_t threadMaxThreshold)
{
	if (checkPoolStatus())
		return;

	if (poolMode_ == PoolMode::CACHED_MODE)
	{
		this->threadSizeThreshold_ = threadMaxThreshold;
	}
}



// �����̺߳���,�̳߳��е��̴߳������������������,�൱���������߳�
void ThreadPool::threadFunc(int threadID)
{
	// ��ȡ��ǰ�̵߳�һ��ִ��ʱ��
	auto lastTime = std::chrono::high_resolution_clock().now();
	while (true)
	{
		std::shared_ptr<Task> task;
		// �Ȼ�ȡ��
		std::unique_lock<std::mutex> lock(taskMutex_);
		std::cout << "tid: " << std::this_thread::get_id() << "�����Ի�ȡ����" << std::endl;

		while (taskQue_.size() == 0)
		{
			// û�����ˣ��̳߳�Ҳ��������
			if (!isPoolRunning_)
			{
				threads_.erase(threadID);
				std::cout << "tid: " << std::this_thread::get_id() << " exit!" << std::endl;
				exitCond_.notify_all();
				return;
			}

			// cached mode ���ܴ����˺ܶ��̣߳�����60s����Ҫ�����߳�
			if (poolMode_ == PoolMode::CACHED_MODE)
			{
				// ����������ʱ����
				if (std::cv_status::timeout == notEmpty_.wait_for(lock, std::chrono::seconds(1)))
				{
					// ���㵱ǰʱ��
					auto now = std::chrono::high_resolution_clock().now();
					// ������ʱ��
					auto duration = std::chrono::duration_cast<std::chrono::seconds>(now - lastTime);
					if (duration.count() >= THREAD_MAX_IDLE_TIME && curThreadSize_ > initThreadSize_)
					{
						// �������ʱ�䣬�����̣߳��ͷ���Դ
						threads_.erase(threadID);
						curThreadSize_--;
						idleThreadSize_--;
						std::cout << "threadID: " << std::this_thread::get_id() << " exit!" << std::endl;
						return;		// �̺߳����������߳���ȻҲ�ͽ�����
					}
				}
			}
			else
			{
				// �ȴ�notEmpty_
				notEmpty_.wait(lock);
			}
		}
		// ������ͼ���ִ��
		idleThreadSize_--;		// �߳�ִ�У������߳�--
		std::cout << "tid: " << std::this_thread::get_id() << "����ȡ����ɹ�" << std::endl;

		// �����������ȡһ���������
		task = taskQue_.front();
		taskQue_.pop();
		taskSize_--;

		// ���������Ȼ���գ�֪ͨ����������
		if (taskQue_.size() > 0)
		{
			notEmpty_.notify_all();
		}

		// ֪ͨ�����߿�����������������������
		notFull_.notify_all();		
		
		if (task != nullptr ) 
			task->exec();  // ��ǰ�̸߳���ִ���������
		idleThreadSize_++;	// ����ִ���꣬�����̸߳���++
		lastTime = std::chrono::high_resolution_clock().now();  // �������һ��ִ��ʱ��
	}
}

///////////////////////// Task���

Task::Task()
	: result_(nullptr)
{

}

void Task::exec()
{
	if (result_ != nullptr)
	{
		result_->setVal(run());		// ���﷢����̬����
	}
}

void Task::setResult(Result* result)
{
	result_ = result;
}


///////////////////////// �߳������

int Thread::generateID_ = 0;

Thread::Thread(Func func)
	: func_(func)
{
	threadID_ = generateID_++;
}

Thread::~Thread()
{

}

// �߳���������
void Thread::start()
{
	std::thread t(func_, threadID_);	// ����һ���߳�ִ���̺߳���
	t.detach();							// �����߳�
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

	sem_.wait();	// �ȴ������߳�ִ�н����������û��߳�
	return std::move(any_);
}

void Result::setVal(Any any)
{
	this->any_ = std::move(any);
	sem_.post();   // ������Դ
}
