#pragma once
#include<mutex>
#include<queue>
#include<vector>
#include<functional>
#include<thread>
#include<future>

#include<iostream>


/*
 * Event类
 * 封装事件与优先级
 * 返回值：
 * f_	事件
 * compare_flag	优先级，我们设置默认为1，越大优先级越高
 */
class Event {
public:
	std::function<void()> f_;
	int compare_flag;

	Event(int compare_flag) :f_([]() {}), compare_flag(compare_flag) {}

	//此处的f_认为是通过包装而来，坚持使用移动语义处理std::function是有必要的
	Event(std::function<void()> f_, int compare_flag = 1)
		:f_(std::move(f_)), compare_flag(compare_flag) {
	}

	//实现移动构造和移动赋值
	Event(Event&& e_)noexcept :f_(std::move(e_.f_)), compare_flag(e_.compare_flag) {}

	//此处需要放开拷贝构造，因为优先队列不允许移动top中的数据
	Event(const Event& e_) :f_(e_.f_), compare_flag(e_.compare_flag) {}

	Event& operator=(Event&& e_)noexcept {
		if (this != &e_) {
			f_ = std::move(e_.f_);
			compare_flag = e_.compare_flag;
		}
		return *this;
	}

	Event& operator=(const Event& e_) {
		if (this != &e_) {
			f_ = e_.f_;
			compare_flag = e_.compare_flag;
		}
	};

};
/*
 * 提供类型别名，提供比较优先级方式
 */
using queue_typename = std::shared_ptr<Event>;
using queue_vector = std::vector<std::shared_ptr<Event>>;

struct EventComparator {
	bool operator()(const queue_typename& a, const queue_typename& b) const {
		return a->compare_flag < b->compare_flag;  // 直接比较数值，避免依赖operator<
	}
};

using queue_ = std::priority_queue<queue_typename, queue_vector, EventComparator>;

/*
 * safeQueue类
 * 封装队列，提供安全加锁的队列函数，在其中实现等待和唤醒
 *
 */
class safeQueue {
private:
	//底层使用vector存储，存储类性设置为用共享指针管理的Event
	queue_ q_;
	std::mutex m_;
	std::condition_variable cv_;

public:


	bool empty_() {
		std::unique_lock<std::mutex> l_(m_);

		return q_.empty();
	}

	size_t size() {
		std::unique_lock<std::mutex> l_(m_);

		return q_.size();
	}

	//入队函数，负责唤醒
	void enqueue(queue_typename&& e_) {
		{
			std::unique_lock<std::mutex> l_(m_);
			q_.emplace(e_);
		}
		cv_.notify_one();
	}


	void notify_all_() {
		cv_.notify_all();
	}

	/**
	* 提供出队函数
	* 传入一个智能指针，在函数内通过top获取值
	* 由线程类传入是否停止的标志
	* 返回布尔类型
	*
	*/
	bool dequeue(const std::atomic<bool>& stopflag, queue_typename& e_) {
		std::unique_lock<std::mutex> l_(m_);
		cv_.wait(l_, [this, &stopflag]() {return stopflag.load() || !q_.empty(); });

		if (stopflag.load() && q_.empty()) {
			return false;
		}
		//top获取到的是个const，我们无法移动它，强行移动会破坏其结构
		//我不得不改变存储策略，存储指针在里面
		e_ = q_.top();
		q_.pop();
		return true;
	}
};


/**
* 线程池主类
*
*
*/
class thread_pool {
private:
	//存储线程池所用的容器
	std::vector<std::thread> v_;
	//封装的队列对象
	safeQueue s_q;
	//线程池停止标志
	std::atomic<bool> stop{ false };

	//最大支持线程数（会根据硬件支持初始化，不会允许调整）
	int max_poolnum;
	//最小支持线程数（会默认为4，不会允许调整）
	int min_poolnum;
	//是否支持系统自动调整线程数量（会在构造函数初始化，初始化后一般不会修改）
	bool autoset_Poolnum;

	//记录：当前正在执行线程数量
	std::atomic<int> cur_workpoolnum{ 0 };
	//记录：目前创建的线程数量
	std::atomic<int> cur_setpoolnum{ 0 };
	//记录：目前队列中存储的任务数量
	std::atomic<int> cur_tasknum{ 0 };
	//调整线程数量互斥锁
	std::mutex m_;

	//创建一个vector，只存放一条线程，这条线程专门用来删除“停止线程”
	std::vector<std::thread> del_thread;
	//创建一个vector，用来存储“已经要停止”的id
	std::vector<std::thread::id> del_id_list;
	//为此线程设置互斥锁和条件变量
	std::mutex vm_;
	std::condition_variable vcv_;


	//这个函数属于线程池中，单条线程的lambda表达式中事件，它会创建一个无限循环，用来获取线程
	void work_pool() {
		std::shared_ptr<Event> l_se;
		while (true) {
			if (s_q.dequeue(stop, l_se)) {
				//当返回true，代表取到值并且删除了队列中任务
				--cur_tasknum;
				if (l_se->compare_flag == -1) {
					//进入这个条件的是特殊任务，在这里获取当前线程id，存入vector，并唤醒删除线程，随后return此线程
					//之所以搞这么麻烦，是因为我们不能让线程自己删除自己，而且删除前需要join，会造成死锁
					//这种方式处理，不会和stop冲突，即便队列中存在一条特殊任务，处理完之后就可以进入stop流程。

					//执行特殊任务不算执行任务，不用特意处理cur_workpoolnum
					{
						std::lock_guard<std::mutex> llm_(vm_);
						del_id_list.push_back(std::this_thread::get_id());
					}
					vcv_.notify_one();
					return;
				}
			}
			else {
				//接收到stop信号，停止此线程
				return;
			}
			//执行任务
			try {
				++cur_workpoolnum;
				l_se->f_();
				--cur_workpoolnum;
			}
			catch (...) {
				--cur_workpoolnum;
				throw;
			}
		}
	}

	//此函数每执行一次，只会创建一条线程。
	//构造函数会循环调用它，来在线程池中创建多条线程
	//它也可以用来单独调用增加新的线程
	void addpool() {
		//此函数执行一次，只会添加一条线程
		if (cur_setpoolnum < max_poolnum) {
			++cur_setpoolnum;
		}
		else {
			return;
		}
		std::lock_guard<std::mutex> l_(m_);
		v_.emplace_back(std::thread([this]() {
			//此函数内部应该是一个独立线程，应该不会受到当前线程的锁影响
			work_pool();
			}));
	}

	//删除线程
	//此函数执行一次，构造一个特殊事件，存入队列，等待线程池处理，获取到事件的线程被删除
	//一般来说，此函数只会在线程池很充裕的情况下执行
	void delpool() {
		//如果此时停止信号已经发出，就不要再往里面放置特殊事件了
		if (stop.load()) { return; }
		s_q.enqueue(
			std::make_shared<Event>(
				Event(-1)
			)
		);
	}


	//真正负责删除的函数
	//此函数构造了一个特殊线程，长期运行，会检测 一个特定容器，是否存在“线程池中需要被删除的线程”
	//如果不存在，它会暂时wait，等待唤醒
	//唤醒后，会从 一个特定容器 中取出线程id，将它从线程池中删除
	void startdelthread() {
		del_thread.emplace_back(std::thread([this]() {
			while (true) {
				std::unique_lock<std::mutex> del_l(vm_);
				vcv_.wait(del_l, [this]() {return stop.load() || !del_id_list.empty(); });
				//如果检测通过，说明线程池正在关闭，此时我应该停止删除线程，剩下的让线程池去处理
				if (stop.load() && del_id_list.empty()) {
					return;
				}

				//但如果这里已经开始处理，我应该在关闭函数中join此线程，等它结束后再处理
				//一般情况下，这里只有一条，有了就会被立马处理
				for (size_t i = 0; i < del_id_list.size(); i++)
				{
					std::lock_guard<std::mutex> l_(m_);
					auto lit_ = std::find_if(v_.begin(), v_.end(), [this, i](const std::thread& lt_) {
						return 	lt_.get_id() == del_id_list[i];
						});
					if (lit_ != v_.end()) {
						//这种线程一般会直接结束，不可能存在这种情况，但为了代码健壮性，等一下也无妨
						if (lit_->joinable()) {
							lit_->join();
						}
						v_.erase(lit_);

						--cur_setpoolnum;//线程彻底删除，目前创建线程数-1
					}
				}
				//删除完毕，清空待删除列表
				{
					std::lock_guard<std::mutex> l_(m_);
					del_id_list.clear();
				}
			}
			}));
	}


public:
	//构造函数，强制输入是否要自动动态调整
	//该构造函数会根据硬件调整最大线程数，但不会允许用户自行调整。如果有需要可改这里的代码
	thread_pool(bool autoset_Poolnum, size_t number_ = 4) {
		this->autoset_Poolnum = autoset_Poolnum;
		min_poolnum = 4;
		max_poolnum = std::thread::hardware_concurrency() * 2;

		if (number_ < min_poolnum) { number_ = min_poolnum; }
		if (number_ > max_poolnum) { number_ = max_poolnum; }

		v_.reserve(max_poolnum);//避免扩容

		for (size_t i = 0; i < number_; i++)
		{
			addpool();
		}

		startdelthread();
	}



	//重载函数，默认优先级为1
	template<class F, class... Args>
	auto addtask(F&& f, Args&& ...args) -> std::future<decltype(f(args...))> {
		return addtask(1, std::forward<F>(f), std::forward<Args>(args)...);
	}


	/*
	* addtask 添加任务
	* 如果不提供优先级参数，会调用上面的重载函数
	* 参数 int compare_flag：优先级，默认为1，不能为负数，否则会对线程池造成错误
	*/
	template<class F, class... Args>
	auto addtask(int compare_flag, F&& f, Args&& ...args) -> std::future<decltype(f(args...))> {
		using return_type = decltype(f(args...));

		//用通用方式包装，获取future
		auto task = std::make_shared<std::packaged_task<return_type()>>(
			std::bind(std::forward<F>(f), std::forward<Args>(args)...)
		);
		std::future<return_type> res = task->get_future();

		//此处我们通过队列封装线程，并且实现唤醒逻辑，队列已经实现了锁，stop也原子处理
		if (stop.load()) {
			throw std::runtime_error("enqueue on stopped thread_pool");
		}
		//提取智能指针内部的函数，类型抹除，构造一个Event类型对象存入
		//这样写更清晰
		s_q.enqueue(
			std::make_shared<Event>(
				Event(
					[task]() {(*task)(); },
					compare_flag
				)
			)
		);
		++cur_tasknum;

		//以下代码检查是否要动态调整线程数量
		if (autoset_Poolnum) {
			// 检查是否需要增加线程
			if (cur_tasknum > 3 && cur_setpoolnum < max_poolnum) {
				//说明此时队列中等待任务比较多，增加一条线程试试
				addpool();
			}

			// 检查是否需要减少线程
			if (cur_tasknum == 0 && cur_setpoolnum > min_poolnum &&
				(cur_setpoolnum - cur_workpoolnum) > cur_setpoolnum / 2) {
				//说明此时线程池比较充裕，减少一条试试
				delpool();
			}
		}
		/*
		{
			std::lock_guard<std::mutex> l_l(vm_);
			std::cout << "当前线程数：" << cur_setpoolnum << std::endl;
		}
		*/
		return res;
	}

	//可以手动关闭的线程池
	void shutdown() {
		stop.store(true);
		//唤醒所有线程
		s_q.notify_all_();
		vcv_.notify_one();//只有一条，all不all的无所谓
		//等待任务执行完毕
		for (size_t i = 0; i < v_.size(); i++)
		{
			if (v_[i].joinable()) {
				v_[i].join();
			}
		}

		//我确信它有且只有一条
		if (del_thread[0].joinable()) {
			del_thread[0].join();
		}
	}

	//当用户忘记关闭时关闭
	~thread_pool() {
		shutdown();
	}
};


