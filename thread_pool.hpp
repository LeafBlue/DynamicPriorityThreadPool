#pragma once
#include<mutex>
#include<queue>
#include<vector>
#include<functional>
#include<thread>
#include<future>

#include<iostream>


/*
 * Event��
 * ��װ�¼������ȼ�
 * ����ֵ��
 * f_	�¼�
 * compare_flag	���ȼ�����������Ĭ��Ϊ1��Խ�����ȼ�Խ��
 */
class Event {
public:
	std::function<void()> f_;
	int compare_flag;

	Event(int compare_flag) :f_([]() {}), compare_flag(compare_flag) {}

	//�˴���f_��Ϊ��ͨ����װ���������ʹ���ƶ����崦��std::function���б�Ҫ��
	Event(std::function<void()> f_, int compare_flag = 1)
		:f_(std::move(f_)), compare_flag(compare_flag) {
	}

	//ʵ���ƶ�������ƶ���ֵ
	Event(Event&& e_)noexcept :f_(std::move(e_.f_)), compare_flag(e_.compare_flag) {}

	//�˴���Ҫ�ſ��������죬��Ϊ���ȶ��в������ƶ�top�е�����
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
 * �ṩ���ͱ������ṩ�Ƚ����ȼ���ʽ
 */
using queue_typename = std::shared_ptr<Event>;
using queue_vector = std::vector<std::shared_ptr<Event>>;

struct EventComparator {
	bool operator()(const queue_typename& a, const queue_typename& b) const {
		return a->compare_flag < b->compare_flag;  // ֱ�ӱȽ���ֵ����������operator<
	}
};

using queue_ = std::priority_queue<queue_typename, queue_vector, EventComparator>;

/*
 * safeQueue��
 * ��װ���У��ṩ��ȫ�����Ķ��к�����������ʵ�ֵȴ��ͻ���
 *
 */
class safeQueue {
private:
	//�ײ�ʹ��vector�洢���洢��������Ϊ�ù���ָ������Event
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

	//��Ӻ�����������
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
	* �ṩ���Ӻ���
	* ����һ������ָ�룬�ں�����ͨ��top��ȡֵ
	* ���߳��ഫ���Ƿ�ֹͣ�ı�־
	* ���ز�������
	*
	*/
	bool dequeue(const std::atomic<bool>& stopflag, queue_typename& e_) {
		std::unique_lock<std::mutex> l_(m_);
		cv_.wait(l_, [this, &stopflag]() {return stopflag.load() || !q_.empty(); });

		if (stopflag.load() && q_.empty()) {
			return false;
		}
		//top��ȡ�����Ǹ�const�������޷��ƶ�����ǿ���ƶ����ƻ���ṹ
		//�Ҳ��ò��ı�洢���ԣ��洢ָ��������
		e_ = q_.top();
		q_.pop();
		return true;
	}
};


/**
* �̳߳�����
*
*
*/
class thread_pool {
private:
	//�洢�̳߳����õ�����
	std::vector<std::thread> v_;
	//��װ�Ķ��ж���
	safeQueue s_q;
	//�̳߳�ֹͣ��־
	std::atomic<bool> stop{ false };

	//���֧���߳����������Ӳ��֧�ֳ�ʼ�����������������
	int max_poolnum;
	//��С֧���߳�������Ĭ��Ϊ4���������������
	int min_poolnum;
	//�Ƿ�֧��ϵͳ�Զ������߳����������ڹ��캯����ʼ������ʼ����һ�㲻���޸ģ�
	bool autoset_Poolnum;

	//��¼����ǰ����ִ���߳�����
	std::atomic<int> cur_workpoolnum{ 0 };
	//��¼��Ŀǰ�������߳�����
	std::atomic<int> cur_setpoolnum{ 0 };
	//��¼��Ŀǰ�����д洢����������
	std::atomic<int> cur_tasknum{ 0 };
	//�����߳�����������
	std::mutex m_;

	//����һ��vector��ֻ���һ���̣߳������߳�ר������ɾ����ֹͣ�̡߳�
	std::vector<std::thread> del_thread;
	//����һ��vector�������洢���Ѿ�Ҫֹͣ����id
	std::vector<std::thread::id> del_id_list;
	//Ϊ���߳����û���������������
	std::mutex vm_;
	std::condition_variable vcv_;


	//������������̳߳��У������̵߳�lambda���ʽ���¼������ᴴ��һ������ѭ����������ȡ�߳�
	void work_pool() {
		std::shared_ptr<Event> l_se;
		while (true) {
			if (s_q.dequeue(stop, l_se)) {
				//������true������ȡ��ֵ����ɾ���˶���������
				--cur_tasknum;
				if (l_se->compare_flag == -1) {
					//������������������������������ȡ��ǰ�߳�id������vector��������ɾ���̣߳����return���߳�
					//֮���Ը���ô�鷳������Ϊ���ǲ������߳��Լ�ɾ���Լ�������ɾ��ǰ��Ҫjoin�����������
					//���ַ�ʽ���������stop��ͻ����������д���һ���������񣬴�����֮��Ϳ��Խ���stop���̡�

					//ִ������������ִ�����񣬲������⴦��cur_workpoolnum
					{
						std::lock_guard<std::mutex> llm_(vm_);
						del_id_list.push_back(std::this_thread::get_id());
					}
					vcv_.notify_one();
					return;
				}
			}
			else {
				//���յ�stop�źţ�ֹͣ���߳�
				return;
			}
			//ִ������
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

	//�˺���ÿִ��һ�Σ�ֻ�ᴴ��һ���̡߳�
	//���캯����ѭ���������������̳߳��д��������߳�
	//��Ҳ���������������������µ��߳�
	void addpool() {
		//�˺���ִ��һ�Σ�ֻ�����һ���߳�
		if (cur_setpoolnum < max_poolnum) {
			++cur_setpoolnum;
		}
		else {
			return;
		}
		std::lock_guard<std::mutex> l_(m_);
		v_.emplace_back(std::thread([this]() {
			//�˺����ڲ�Ӧ����һ�������̣߳�Ӧ�ò����ܵ���ǰ�̵߳���Ӱ��
			work_pool();
			}));
	}

	//ɾ���߳�
	//�˺���ִ��һ�Σ�����һ�������¼���������У��ȴ��̳߳ش�����ȡ���¼����̱߳�ɾ��
	//һ����˵���˺���ֻ�����̳߳غܳ�ԣ�������ִ��
	void delpool() {
		//�����ʱֹͣ�ź��Ѿ��������Ͳ�Ҫ����������������¼���
		if (stop.load()) { return; }
		s_q.enqueue(
			std::make_shared<Event>(
				Event(-1)
			)
		);
	}


	//��������ɾ���ĺ���
	//�˺���������һ�������̣߳��������У����� һ���ض��������Ƿ���ڡ��̳߳�����Ҫ��ɾ�����̡߳�
	//��������ڣ�������ʱwait���ȴ�����
	//���Ѻ󣬻�� һ���ض����� ��ȡ���߳�id���������̳߳���ɾ��
	void startdelthread() {
		del_thread.emplace_back(std::thread([this]() {
			while (true) {
				std::unique_lock<std::mutex> del_l(vm_);
				vcv_.wait(del_l, [this]() {return stop.load() || !del_id_list.empty(); });
				//������ͨ����˵���̳߳����ڹرգ���ʱ��Ӧ��ֹͣɾ���̣߳�ʣ�µ����̳߳�ȥ����
				if (stop.load() && del_id_list.empty()) {
					return;
				}

				//����������Ѿ���ʼ������Ӧ���ڹرպ�����join���̣߳������������ٴ���
				//һ������£�����ֻ��һ�������˾ͻᱻ������
				for (size_t i = 0; i < del_id_list.size(); i++)
				{
					std::lock_guard<std::mutex> l_(m_);
					auto lit_ = std::find_if(v_.begin(), v_.end(), [this, i](const std::thread& lt_) {
						return 	lt_.get_id() == del_id_list[i];
						});
					if (lit_ != v_.end()) {
						//�����߳�һ���ֱ�ӽ����������ܴ��������������Ϊ�˴��뽡׳�ԣ���һ��Ҳ�޷�
						if (lit_->joinable()) {
							lit_->join();
						}
						v_.erase(lit_);

						--cur_setpoolnum;//�̳߳���ɾ����Ŀǰ�����߳���-1
					}
				}
				//ɾ����ϣ���մ�ɾ���б�
				{
					std::lock_guard<std::mutex> l_(m_);
					del_id_list.clear();
				}
			}
			}));
	}


public:
	//���캯����ǿ�������Ƿ�Ҫ�Զ���̬����
	//�ù��캯�������Ӳ����������߳����������������û����е������������Ҫ�ɸ�����Ĵ���
	thread_pool(bool autoset_Poolnum, size_t number_ = 4) {
		this->autoset_Poolnum = autoset_Poolnum;
		min_poolnum = 4;
		max_poolnum = std::thread::hardware_concurrency() * 2;

		if (number_ < min_poolnum) { number_ = min_poolnum; }
		if (number_ > max_poolnum) { number_ = max_poolnum; }

		v_.reserve(max_poolnum);//��������

		for (size_t i = 0; i < number_; i++)
		{
			addpool();
		}

		startdelthread();
	}



	//���غ�����Ĭ�����ȼ�Ϊ1
	template<class F, class... Args>
	auto addtask(F&& f, Args&& ...args) -> std::future<decltype(f(args...))> {
		return addtask(1, std::forward<F>(f), std::forward<Args>(args)...);
	}


	/*
	* addtask �������
	* ������ṩ���ȼ��������������������غ���
	* ���� int compare_flag�����ȼ���Ĭ��Ϊ1������Ϊ�������������̳߳���ɴ���
	*/
	template<class F, class... Args>
	auto addtask(int compare_flag, F&& f, Args&& ...args) -> std::future<decltype(f(args...))> {
		using return_type = decltype(f(args...));

		//��ͨ�÷�ʽ��װ����ȡfuture
		auto task = std::make_shared<std::packaged_task<return_type()>>(
			std::bind(std::forward<F>(f), std::forward<Args>(args)...)
		);
		std::future<return_type> res = task->get_future();

		//�˴�����ͨ�����з�װ�̣߳�����ʵ�ֻ����߼��������Ѿ�ʵ��������stopҲԭ�Ӵ���
		if (stop.load()) {
			throw std::runtime_error("enqueue on stopped thread_pool");
		}
		//��ȡ����ָ���ڲ��ĺ���������Ĩ��������һ��Event���Ͷ������
		//����д������
		s_q.enqueue(
			std::make_shared<Event>(
				Event(
					[task]() {(*task)(); },
					compare_flag
				)
			)
		);
		++cur_tasknum;

		//���´������Ƿ�Ҫ��̬�����߳�����
		if (autoset_Poolnum) {
			// ����Ƿ���Ҫ�����߳�
			if (cur_tasknum > 3 && cur_setpoolnum < max_poolnum) {
				//˵����ʱ�����еȴ�����Ƚ϶࣬����һ���߳�����
				addpool();
			}

			// ����Ƿ���Ҫ�����߳�
			if (cur_tasknum == 0 && cur_setpoolnum > min_poolnum &&
				(cur_setpoolnum - cur_workpoolnum) > cur_setpoolnum / 2) {
				//˵����ʱ�̳߳رȽϳ�ԣ������һ������
				delpool();
			}
		}
		/*
		{
			std::lock_guard<std::mutex> l_l(vm_);
			std::cout << "��ǰ�߳�����" << cur_setpoolnum << std::endl;
		}
		*/
		return res;
	}

	//�����ֶ��رյ��̳߳�
	void shutdown() {
		stop.store(true);
		//���������߳�
		s_q.notify_all_();
		vcv_.notify_one();//ֻ��һ����all��all������ν
		//�ȴ�����ִ�����
		for (size_t i = 0; i < v_.size(); i++)
		{
			if (v_[i].joinable()) {
				v_[i].join();
			}
		}

		//��ȷ��������ֻ��һ��
		if (del_thread[0].joinable()) {
			del_thread[0].join();
		}
	}

	//���û����ǹر�ʱ�ر�
	~thread_pool() {
		shutdown();
	}
};


