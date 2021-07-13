#include <iostream>
#include <chrono>
#include <thread>
#include <future>
#include <execution>
#include <algorithm>
#include <vector>
#include <random>
#include <stout/adaptor.h>


using namespace std;
using namespace chrono_literals;
using ull = unsigned long long;

class MyTimer
{
private:
    decltype(std::chrono::high_resolution_clock::now()) time_begin;
public:
    ~MyTimer(){}
    MyTimer(){}
    MyTimer(const MyTimer &other) = delete;

    void start()
    {
        time_begin = std::chrono::high_resolution_clock::now();
    }

    void stop()
    {
        auto time_current = std::chrono::high_resolution_clock::now();
        std::chrono::duration<float> time_duration = 
            time_current - time_begin;
        std::cout << "Time passed: " << time_duration.count() << std::endl;
    }
};

int do_some_tough_calcs1()
{
    cout << this_thread::get_id() << " thread started!" << endl;
    std::this_thread::sleep_for(2s);
    cout << this_thread::get_id() << " thread finished!" << endl;
    return 1;
}

int do_some_tough_calcs2()
{
    cout << this_thread::get_id() << " thread started!" << endl;
    std::this_thread::sleep_for(5s);
    cout << this_thread::get_id() << " thread finished!" << endl;
    return 2;
}

void do_some_tough_calcs3(std::promise<int> &&p)
{
    this_thread::sleep_for(3.5s);
    p.set_value(1);
}

void foo()
{
    for(int i = 0; i < 10; ++i)
    {
        cout << i << endl;
    }
}

void find_sum_odds1(std::promise<ull>& prom)
{
    std::cout << this_thread::get_id() << " find_sum_odds1 is working!" 
                                       << std::endl;
    ull sum = 0;
    for(ull i = 0; i < 19'000'000'00; ++i)
    {
        if(i%2 != 0)
        {
            sum += i;
        }
    }
    prom.set_value(sum);
}

auto find_sum_odds2()
{
   std::cout << this_thread::get_id() << " find_sum_odds2 is working!" 
                                       << std::endl; 
   ull sum = 0;
   for(ull i = 0; i < 19'000'000'00; ++i)
   {
       if(i%2 != 0)
       {
           sum += i;
       }
   }
   return sum;
}

std::future<int> Foo()
{
    std::promise<int> promise;
    std::future<int> fu = promise.get_future();

    std::thread t1(do_some_tough_calcs3, std::move(promise));

    t1.detach();

    return fu;
}

int main()
{
    MyTimer my_timer;
    my_timer.start();

    //---------------------------------------------------------------------
    //---------------------------------------------------------------------
    // this_thread::sleep_for(2s);
    // my_timer.stop();

    // thread t1([]{
    //     auto res = do_some_tough_calcs1();
    // });

    //foo();

    // thread t2([]{
    //     auto res = do_some_tough_calcs2();
    // });
    // t1.join();
    // t2.detach();
    //---------------------------------------------------------------------
    //---------------------------------------------------------------------

    //do_some_tough_calcs1();
    //do_some_tough_calcs2();

    //---------------------------------------------------------------------
    //---------------------------------------------------------------------
    // vector<int> v(50000000);
    // mt19937 gen;
    // uniform_int_distribution<int> dis(0, 100000);
    // auto rand_num ([dis, gen] () mutable {return dis(gen);});
    // std::generate(std::execution::par, v.begin(), v.end(), rand_num);
    //---------------------------------------------------------------------
    //---------------------------------------------------------------------


    //---------------------------------------------------------------------
    //---------------------------------------------------------------------
    //cout << thread::hardware_concurrency() << endl;
    //---------------------------------------------------------------------
    //---------------------------------------------------------------------
    

    //---------------------------------------------------------------------
    //---------------------------------------------------------------------
    // cout << find_sum_odds() << endl;
    //---------------------------------------------------------------------
    //---------------------------------------------------------------------


    //---------------------------------------------------------------------
    //---------------------------------------------------------------------
    // std::cout << this_thread::get_id() << " main working!" << std::endl; 

    // std::promise<ull> odds_sum;
    // std::future<ull> future_value = odds_sum.get_future();
    // std::thread t1(find_sum_odds1, std::ref(odds_sum));

    // std::cout << "waiting for the result!!" << std::endl;

    // auto res = future_value.get();

    // std::cout << "Res = " << res << std::endl;
    
    // t1.detach();
    // my_timer.stop();

    // std::promise<int> p1;
    // std::promise<int> p2 = std::move(p1);
    //---------------------------------------------------------------------
    //---------------------------------------------------------------------

    //---------------------------------------------------------------------
    //---------------------------------------------------------------------
    // std::cout << this_thread::get_id() << " main working!" << std::endl;
    // std::future<ull> fu_res = std::async(std::launch::async, find_sum_odds2);
    // auto res = fu_res.get();
    // std::cout << res << std::endl;
    //---------------------------------------------------------------------
    //---------------------------------------------------------------------


    //---------------------------------------------------------------------
    //---------------------------------------------------------------------
    auto fu = Foo();
    //auto res = fu.get(); //blocking
    //cout << "res = " << res << endl;
    cout << "Ji\n";
    //---------------------------------------------------------------------
    //---------------------------------------------------------------------




    return 0;
}