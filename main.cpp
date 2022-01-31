#include <atomic>
#include <cassert>
#include <cinttypes>
#include <condition_variable>
#include <deque>
#include <functional>
#include <future>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <thread>

template <typename T>
class Channel
{
    std::deque<T>           queue;
    std::condition_variable cond;
    mutable std::mutex      mtx;
    std::atomic_bool        done;

public:
    using value_type = T;

    Channel() { done.store(false); };
    Channel(const Channel&) = delete;
    Channel(Channel&&)      = delete;
    Channel& operator=(const Channel&) = delete;
    Channel& operator=(Channel&&) = delete;
    ~Channel() { shutdown(); }

    void shutdown()
    {
        done.store(true, std::memory_order_release);
        cond.notify_all();
    }

    void send(T&& t)
    {
        std::lock_guard<decltype(mtx)> lg(mtx);
        queue.push_back(std::forward<T>(t));
        cond.notify_one();
    }

    std::optional<T> recv()
    {
        while (!done.load(std::memory_order_acquire)) { // loop to account for spurious
                                                        // wake-ups of the conditional variable
            std::unique_lock<decltype(mtx)> lg(mtx);
            if (!queue.empty()) {
                const auto res = std::make_optional(std::move(queue.front()));
                queue.pop_front();
                return res;
            }
            cond.wait(lg);
            if (!queue.empty()) {
                const auto res = std::make_optional(std::move(queue.front()));
                queue.pop_front();
                return res;
            }
        }
        return std::nullopt;
    }

    std::optional<T> try_recv()
    {
        std::lock_guard<decltype(mtx)> lg(mtx);
        if (!queue.empty()) {
            const auto res = std::make_optional(std::move(queue.front()));
            queue.pop_front();
            return res;
        } else {
            return std::nullopt;
        }
    }
};

class SubsystemA
{
    std::string valueA;

    SubsystemA(std::string val) : valueA(std::move(val)) {}

public:
    SubsystemA()                  = delete;
    SubsystemA(const SubsystemA&) = delete;
    SubsystemA(SubsystemA&&)      = delete;
    SubsystemA& operator=(const SubsystemA&) = delete;
    SubsystemA& operator=(SubsystemA&&) = delete;

    std::string concate_and_get(std::string B)
    {
        valueA += B;
        return valueA;
    }

    std::size_t get_size() const { return valueA.size(); }

    std::string sub_value(std::size_t start_point, std::size_t length) const
    {
        return valueA.substr(start_point, length);
    }

    static std::unique_ptr<SubsystemA> Make(std::string initialValue)
    {
        return std::unique_ptr<SubsystemA>(new SubsystemA{initialValue});
    }
};

class SubsystemB
{
    std::uint64_t value;

    SubsystemB() : value(13) {}

public:
    SubsystemB(const SubsystemB&) = delete;
    SubsystemB(SubsystemB&&)      = delete;
    SubsystemB& operator=(const SubsystemB&) = delete;
    SubsystemB& operator=(SubsystemB&&) = delete;

    std::uint64_t get_value() const { return value; }

    std::uint64_t add_and_get(std::uint64_t v)
    {
        value += v;
        return value;
    }

    static std::unique_ptr<SubsystemB> Make() { return std::unique_ptr<SubsystemB>(new SubsystemB{}); }
};

template <typename WT>
class Wrapper
{
    Channel<std::function<void(WT*)>> ch;

    std::unique_ptr<WT>          wrapped;
    std::unique_ptr<std::thread> thread;

    std::atomic_bool done;

    Wrapper(std::unique_ptr<WT>&& WrappedIn) : wrapped(std::move(WrappedIn))
    {
        done.store(false, std::memory_order_seq_cst);
        thread = std::make_unique<std::thread>([this]() {
            while (!done.load(std::memory_order_acquire)) {
                auto f_data_op = ch.recv();
                if (!f_data_op) {
                    break;
                }
                (*f_data_op)(wrapped.get());
            }
        });
    }

public:
    Wrapper(const Wrapper&) = delete;
    Wrapper(Wrapper&&)      = delete;
    Wrapper& operator=(const Wrapper&) = delete;
    Wrapper& operator=(Wrapper&&) = delete;
    ~Wrapper()
    {
        ch.shutdown();
        done.store(true, std::memory_order_release);
        if (thread)
            thread->join();
    }

    template <typename... Params>
    static std::unique_ptr<Wrapper<WT>> MakeWrapper(Params... params)
    {
        std::unique_ptr<WT> wrapped = WT::Make(std::forward<Params>(params)...);
        return std::unique_ptr<Wrapper<WT>>(new Wrapper<WT>(std::move(wrapped)));
    }

    template <typename ReturnType, typename Func, typename... Params>
    std::future<ReturnType> call(Func f, Params... params)
    {
        assert(wrapped);
        std::shared_ptr<std::promise<ReturnType>> promise = std::make_shared<std::promise<ReturnType>>();
        std::future<ReturnType>                   future  = promise->get_future();
        auto mem_func_with_params = std::bind(f, std::placeholders::_1, std::forward<Params>(params)...);
        auto mem_func_with_return = [promise, mem_func_with_params](WT* wrapper) {
            promise->set_value(mem_func_with_params(wrapper));
        };
        ch.send(mem_func_with_return);
        return future;
    }
};

template <typename T, typename U>
void AssertEq(T&& a, U&& b, int line)
{
    if (a != static_cast<T>(b)) {
        std::stringstream ss;
        ss << "Equality check failed for \"" << a << "\" and \"" << b << "\" from line " << line;
        std::cerr << ss.str() << std::endl;
        throw std::runtime_error(ss.str());
    }
}

#define ASSERT_EQUALITY(a, b) AssertEq(a, b, __LINE__);

int main()
{
    auto wrapperA = Wrapper<SubsystemA>::MakeWrapper(std::string("abc"));
    auto wrapperB = Wrapper<SubsystemB>::MakeWrapper();

    // two calls to SubsystemA
    std::future<std::string> c =
        wrapperA->call<std::string>(&SubsystemA::concate_and_get, std::string("xyz"));
    ASSERT_EQUALITY(c.get(), "abcxyz");

    std::future<std::string> d = wrapperA->call<std::string>(&SubsystemA::sub_value, 1, 3);
    ASSERT_EQUALITY(d.get(), "bcx");

    // two calls to SubsystemB
    std::future<std::uint64_t> f = wrapperB->call<std::uint64_t>(&SubsystemB::get_value);
    ASSERT_EQUALITY(f.get(), 13);

    std::future<std::uint64_t> e = wrapperB->call<std::uint64_t>(&SubsystemB::add_and_get, 3);
    ASSERT_EQUALITY(e.get(), 16);

    return EXIT_SUCCESS;
}
