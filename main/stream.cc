#include "stout/stream.h"
#include <cassert>
#include <chrono>
#include <iostream>
#include <list>
#include <thread>
#include "stout/adaptor.h"
#include "stout/callback.h"
#include "stout/conditional.h"
#include "stout/eventual.h"
#include "stout/just.h"
#include "stout/lambda.h"
#include "stout/raise.h"
#include "stout/reduce.h"
#include "stout/repeat.h"
#include "stout/task.h"
#include "stout/then.h"

using namespace std;
using namespace chrono_literals;

using stout::eventuals::Conditional;
using stout::eventuals::done;
using stout::eventuals::emit;
using stout::eventuals::ended;
using stout::eventuals::Eventual;
using stout::eventuals::Just;
using stout::eventuals::Lambda;
using stout::eventuals::Loop;
using stout::eventuals::Map;
using stout::eventuals::next;
using stout::eventuals::Raise;
using stout::eventuals::Reduce;
using stout::eventuals::Repeat;
using stout::eventuals::start;
using stout::eventuals::Stream;
using stout::eventuals::succeed;
using stout::eventuals::Task;
using stout::eventuals::Terminal;
using stout::eventuals::Then;
using stout::eventuals::Transform;

auto Bam() {
    return Repeat(Then([i = 0]() mutable { return Just(i++); }));
}

auto Waz() {
    return Map(Then([](auto &&i) { return Just(to_string(i)); }));
}

auto Wam() {
    return Reduce(std::list<string>(), [](auto &list) {
        return [&list](auto &&s) {
            if (list.size() >= 5) return false;
            else {
                list.push_back(s);
                return true;   // continue looping (i.e., call 'next()')
            }
            // return false; // stop reducing/looping (i.e., call 'ended()')
        };
    });
}

int main(int argc, char **argv) {
    cout << "starting..." << endl;
    cout << "main id = " << this_thread::get_id() << endl;

    // auto foo = [](){
    //     return Stream<int>()
    //         .context(0)
    //         .start([](auto &i, auto &k){
    //             succeed(k);
    //         })
    //         .next([](auto &k, auto &i){
    //             if(i > 4) ended(k);
    //             else{
    //                 cout << "next callback: " << i << endl;
    //                 emit(k, ++i);
    //             }
    //         })
    //         .done([](auto &k, auto &i){
    //             ended(k);
    //         });
    // };

    // auto ee = foo()
    //     | Transform<string>()
    //         .start([](auto &stream, auto &k){
    //             succeed(k);
    //         })
    //         .body([](auto &k, auto &&item){
    //             succeed(k, to_string(item * item));
    //         })
    //     | (Loop<list<string>>()
    //         .context(list<string> ())
    //         .start([](auto &list, auto &stream, auto &k){
    //             next(stream);
    //         })
    //         .body([](auto &list, auto &stream, auto &&s){
    //             list.push_back(s);
    //             next(stream);
    //         })
    //         .ended([](auto &list, auto &k){
    //             succeed(k, list);
    //         }))
    //     // | Then([](auto &&list){
    //     //     return Just(list.size());
    //     // })
    //     | Terminal()
    //         .start([](auto &&res){
    //     });
    // //
    // start(ee);

    // auto res = std::move(*e());

    int  a  = 13;

    auto s1 = [&]() {
        return Stream<int>()
                   .context(a)
                   .start([](auto &&val, auto &k) {
                       cout << "start stream" << endl;
                       succeed(k, val);
                       cout << "***" << endl;
                   })
                   .next([](auto &count, auto &k) {
                       cout << ".next is working..." << count << endl;
                       if (count > 0) {
                           emit(k, count--);
                       } else {
                           cout << "ended1" << endl;
                           ended(k);
                       }
                   })
                   .done([&](auto &k, auto &) {
                       cout << "ended!" << endl;
                       ended(k);
                   }) |
               (Loop<int>()
                    .context(0)
                    .body([](auto &sum, auto &stream, auto &&value) {
                        cout << "sum = " << sum << endl;
                        sum += value;
                        next(stream);
                    })
                    .ended([](auto &sum, auto &k) {
                        cout << "ended2" << endl;
                        succeed(k, sum);
                    }));
    };

    // auto res = std::move(*s1());
    // start(s); ??????????????

    //-----------------------------------------------------------------------
    //-----------------------------------------------------------------------
    auto stream2 = []() {
        return Stream<int>()
            .context(3)
            .start([](auto &&value, auto &k) {
                cout << "section code in .start (lambda stream2)" << endl;
                succeed(k, value);
            })
            .next([](auto &&value, auto &k) {
                cout << "section code in .next" << endl;
                if (value > 0) emit(k, value--);
                else
                    ended(k);
            })
            .done([](auto &k, auto &&value) {
                cout << "section code in .done" << endl;
                ended(k);
            });
    };

    auto stream2_transform = []() {
        return Transform<string>()
            .start([](auto &stream, auto &k) { succeed(k); })
            .body(
                [](auto &k, auto &&item) { emit(k, to_string(item * item)); });
    };

    auto e =
        stream2() | stream2_transform() |
        (Loop<list<string>>()
             .context(list<string>{})
             .start([](auto &list, auto &stream, auto &k) { next(stream); })
             .body([](auto &list, auto &stream, auto &&item) {
                 list.push_back(item);
                 next(stream);
             })
             .ended([](auto &list, auto &k) {
                 cout << "list result size = " << list.size() << endl;
                 succeed(k, list);
             }));
    // | Then([](auto &&list){
    //   return Just(list.size());
    // });

    // auto ee = std::move(e)
    //   | Then([](auto &&list){
    //     return Just(list.size());
    //   });

    // auto ee = std::move(e)
    //   | Eventual<size_t>()
    //     .start([](auto &k, auto &&list){
    //       succeed(k, list.size());
    //     });

    // auto res = *e();
    // auto size = *std::move(e);
    //-----------------------------------------------------------------------
    //-----------------------------------------------------------------------

    //-----------------------------------------------------------------------
    //-----------------------------------------------------------------------

    auto stream3 = []() {
        return Repeat(Then([i = 0]() mutable { return Just(i++); }));
    };

    auto s_transform = []() {
        return Map(Then([](auto &&i) { return Just(to_string(i)); }));
    };

    auto s_looping = []() {
        return Reduce(list<string>{}, [](auto &list) {
            return [&list](auto &&item) {
                if (list.size() > 10) return false;
                else {
                    list.push_back(item);
                    return true;
                }
            };
        });
    };

    size_t size =
        *(stream3() | s_transform() | s_looping() | Lambda([](auto &&list) {
              for (const auto &el : list) cout << el << " ";
              cout << endl;
              return list.size();
          }));

    cout << size << endl;

    //-----------------------------------------------------------------------
    //-----------------------------------------------------------------------

    //-----------------------------------------------------------------------
    //-----------------------------------------------------------------------
    auto e_inf = Stream<int>().context(3).next([](auto &cnt, auto &k) {
        // if(cnt-- > 0) emit(k, cnt);
        // else ended(k);
        cout << "body1" << endl;
        emit(k, cnt);
        // cout << ++cnt << endl;
    }) | Map(Then([](auto &&cnt) {
                     cout << "body2" << endl;
                     return Just(cnt + 1);
                 })) |
                 Loop();

    *std::move(e_inf);
    //-----------------------------------------------------------------------
    //-----------------------------------------------------------------------

    // this_thread::sleep_for(100s);
    return cout.good() ? EXIT_SUCCESS : EXIT_FAILURE;
}