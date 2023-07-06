#pragma once
#include<queue>
#include<pthread.h>
using namespace std;

using callback = void(*)(void* arg);
//任务结构体
template<typename T>
struct Task
{
	Task<T>() {
		function = nullptr;
		arg = nullptr;
	}
	Task<T>(callback f, void* arg) {
		this->arg =(T*) arg;
		function = f;
	}
	callback function;
	void* arg;
};

template<typename T>
class TaskQueue
{
public:
	TaskQueue();
	~TaskQueue();

	//添加任务
	void addTask(Task<T> task);
	void addTask(callback f,void *arg);
	//取出任务
	Task<T> takeTask();
	//获取任务个数
	//内联函数,代码块替换，不会压栈
	inline size_t taskNumber() {
		return m_taskQ.size();
	}



private:
	pthread_mutex_t m_mutex;
	queue<Task<T>> m_taskQ;

};

