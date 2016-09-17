// BenchmarkRegex.cpp : Defines the entry point for the console application.
//

#include <vector>
#include <iostream>
#include <iomanip>
#include <string>
#include <cstring>
#include <cmath>
#include <cassert>
#include <sstream>
#include <cstdlib>
#include <chrono>
#include <regex> 
#include <unordered_map>
#include <map>
#include <thread>
#include <mutex>
#include <memory> 

using namespace std;

const int LOOP = 1000000;
const int THREADS = 4;

class timer
{
public: 
	timer() = default;
	void start_timing(const string& text_)
	{
		text = text_;
		begin = chrono::high_resolution_clock::now();
	}
	void stop_timing()
	{
		auto end = chrono::high_resolution_clock::now();
		auto dur = end - begin;
		auto ms = chrono::duration_cast<chrono::milliseconds>(dur).count();
		cout << setw(35) << text << ":" << setw(5) << ms << "ms" << endl;
	}

private:
	string text;
	chrono::steady_clock::time_point begin;
};

#ifdef WIN32

#pragma optimize("", off)
template <class T>
void do_not_optimize_away(T* datum) {
	datum = datum;
}
#pragma optimize("", on)

#else
static void do_not_optimize_away(void* p) { 
	asm volatile("" : : "g"(p) : "memory");
}
#endif

struct singleton
{
public:
	typedef unordered_map<thread::id, unique_ptr<regex> > MapType;
	static void set(const string& reg_exp) { s_reg_exp = reg_exp; }
	static const regex& get()
	{
		lock_guard<mutex> lock(s_mutex);

		thread::id thread_id = this_thread::get_id();
		MapType::iterator it = s_map.find(thread_id);
		if (it == s_map.end())
		{
			prevent_false_sharing(1000);
			s_map[thread_id] = unique_ptr<regex>(new regex(s_reg_exp));
			return *s_map[thread_id];
		}
		return *(it->second);
	}
private:
	static void prevent_false_sharing(size_t size)
	{
		unique_ptr<char[]> p = unique_ptr<char[]>(new char[size]);
		s_vec_temp_alloc.push_back(move(p));
	}
	static MapType s_map;
	static string s_reg_exp;
	static mutex s_mutex;
	static vector< unique_ptr<char[]> > s_vec_temp_alloc;
};
singleton::MapType singleton::s_map;
string singleton::s_reg_exp;
mutex singleton::s_mutex;
vector< unique_ptr<char[]> > singleton::s_vec_temp_alloc;

struct factory
{
public:
	static unique_ptr<regex> get(const string& reg_exp)
	{
		lock_guard<mutex> lock(s_mutex);
		prevent_false_sharing(1000);
		return unique_ptr<regex>(new regex(reg_exp));
	}
private:
	static void prevent_false_sharing(size_t size)
	{
		unique_ptr<char[]> p = unique_ptr<char[]>(new char[size]);
		s_vec_temp_alloc.push_back(move(p));
	}
	static mutex s_mutex;
	static vector< unique_ptr<char[]> > s_vec_temp_alloc;
};
mutex factory::s_mutex;
vector< unique_ptr<char[]> > factory::s_vec_temp_alloc;

const string local_match(const string& text);
const string static_match(const string& text); // not reentrant-safe
const string singleton_match(const string& text);
const string factory_match(const string& text, const regex& regex);

void parallel_invoke(const int size, const int threads, function<void(int, int)> func)
{
	typedef unique_ptr<thread> PtrType;
	vector< PtrType > vec;
	int each = size / threads;

	for (int i = 0; i < threads; ++i)
	{
		if (i == threads - 1) // last thread
		{
			vec.emplace_back(PtrType(new thread(func, each*i, each*(i + 1) + (size % threads) - 1)));
		}
		else
		{
			vec.emplace_back(PtrType(new thread(func, each*i, each*(i + 1) - 1 )));
		}
	}

	for (size_t i = 0; i < vec.size(); ++i)
	{
		vec[i]->join();
	}
}

const string REG_EXP = ".*PRICE:.*US\\$(\\d+\\.\\d+|[-+]*\\d+).*PER SHARE";

int main(int argc, char* argv[])
{
	string str1 = "Zoomer PRICE: US$1.23 PER SHARE";
	string str2 = "Boomer PRICE: US$4.56 PER SHARE";
	
	vector<string> vec;
	vec.push_back(str1);
	vec.push_back(str2);
	
	timer stopwatch;

	stopwatch.start_timing("local regex object");
	for(int j = 0; j < LOOP; ++j)
	{
		for(size_t i = 0; i < vec.size(); ++i)
		{
			do_not_optimize_away(local_match(vec[i]).c_str());
		}
	}
	stopwatch.stop_timing();

	stopwatch.start_timing("static regex object");
	for(int j = 0; j < LOOP; ++j)
	{
		for(size_t i = 0; i < vec.size(); ++i)
		{
			do_not_optimize_away(static_match(vec[i]).c_str());
		}
	}
	stopwatch.stop_timing();

	singleton::set(REG_EXP);
	stopwatch.start_timing("singleton regex object");
	for (int j = 0; j < LOOP; ++j)
	{
		for (size_t i = 0; i < vec.size(); ++i)
		{
			do_not_optimize_away(singleton_match(vec[i]).c_str());
		}
	}
	stopwatch.stop_timing();

	ostringstream os;
	os << "local regex object(" << THREADS << " threads)";
	stopwatch.start_timing(os.str());
	parallel_invoke(LOOP, THREADS, [&vec](int start, int end) {
		for (int j = start; j < end; ++j)
		{
			for (size_t i = 0; i < vec.size(); ++i)
			{
				do_not_optimize_away(local_match(vec[i]).c_str());
			}
		}
	});
	stopwatch.stop_timing();

	os.clear();
	os.str("");
	os << "singleton regex object(" << THREADS << " threads)";
	stopwatch.start_timing(os.str());
	parallel_invoke(LOOP, THREADS, [&vec] (int start, int end) {
		for (int j = start; j < end; ++j)
		{
			for (size_t i = 0; i < vec.size(); ++i)
			{
				do_not_optimize_away(singleton_match(vec[i]).c_str());
			}
		}
	});
	stopwatch.stop_timing();

	os.clear();
	os.str("");
	os << "factory regex object(" << THREADS << " threads)";
	stopwatch.start_timing(os.str());
	parallel_invoke(LOOP, THREADS, [&vec](int start, int end) {
		unique_ptr<regex> ptr = factory::get(REG_EXP);
		const regex& regex = *ptr;

		for (int j = start; j < end; ++j)
		{
			for (size_t i = 0; i < vec.size(); ++i)
			{
				do_not_optimize_away(factory_match(vec[i], regex).c_str());
			}
		}
	});
	stopwatch.stop_timing();
	
	/*
	cout << local_match(str1) << endl;
	cout << local_match(str2) << endl;
	cout << static_match(str1) << endl;
	cout << static_match(str2) << endl;
	singleton::set(REG_EXP);
	cout << singleton_match(str1) << endl;
	cout << singleton_match(str2) << endl;
	const regex regex(REG_EXP);
	cout << factory_match(str1, regex) << endl;
	cout << factory_match(str2, regex) << endl;
	*/

	return 0;
}

const string local_match(const string& text)
{
	string ipo_price = "";
	smatch what;
	const regex regex(REG_EXP);
	if (regex_match(text, what, regex))
	{
		ipo_price = what[1];
	}
	return ipo_price;
}

const string static_match(const string& text) // not reentrant-safe
{
	string ipo_price = "";
	smatch what;
	static const regex regex(REG_EXP);
	if (regex_match(text, what, regex))
	{
		ipo_price = what[1];
	}
	return ipo_price;
}

const string singleton_match(const string& text)
{
	string ipo_price = "";
	smatch what;
	const regex& regex = singleton::get();
	if (regex_match(text, what, regex))
	{
		ipo_price = what[1];
	}
	return ipo_price;
}

const string factory_match(const string& text, const regex& regex)
{
	string ipo_price = "";
	smatch what;
	if (regex_match(text, what, regex))
	{
		ipo_price = what[1];
	}
	return ipo_price;
}
