/*  Periodic Scheduler
 *
 *  From: https://github.com/PokemonAutomation/Arduino-Source
 *
 *      Periodically call a set of callbacks at custom intervals.
 *
 */

#ifndef PokemonAutomation_PeriodicScheduler_H
#define PokemonAutomation_PeriodicScheduler_H

#include <chrono>
#include <map>
#include <mutex>
#include <condition_variable>
#include "Time.h"
#include "AsyncDispatcher.h"
#include "CancellableScope.h"

namespace PokemonAutomation{


//
//  This is the raw (unprotected) data structure that tracks all the events
//  and determines what event should be fired next and when.
//
class PeriodicScheduler{
public:
    //  Returns true if event was successfully added.
    bool add_event(void* event, std::chrono::milliseconds period, WallClock start = current_time());
    void remove_event(void* event);

    //  Returns the next scheduled event. If no events are scheduled, returns WallClock::max().
    WallClock next_event() const;

    //  If an event is before the current timestamp, return it and reschedule for next period.
    //  If nothing is before the current timestamp, return nullptr.
    void* request_next_event(WallClock timestamp = current_time());

private:
    //  "id" is needed to solve ABA problem if the same pointer is removed/re-added.
    struct PeriodicEvent{
        uint64_t id;
        std::chrono::milliseconds period;
    };
    struct SingleEvent{
        uint64_t id;
        void* event;
    };

private:
    uint64_t m_callback_id = 0;
    std::map<void*, PeriodicEvent> m_events;
    std::multimap<WallClock, SingleEvent> m_schedule;
};



//
//  This is the actual callback runner class that will run the callbacks
//  at their specified periods.
//
//  Adding and removing callbacks is thread-safe.
//
class PeriodicRunner : public Cancellable{
public:
    virtual bool cancel(std::exception_ptr exception) noexcept override;

protected:
    PeriodicRunner(AsyncDispatcher& dispatcher);
    bool add_event(void* event, std::chrono::milliseconds period, WallClock start = current_time());
    void remove_event(void* event);
    virtual void run(void* event) noexcept = 0;

private:
    void thread_loop();
protected:
    void stop_thread();

private:
    AsyncDispatcher& m_dispatcher;

    std::mutex m_state_lock;
    PeriodicScheduler m_scheduler;

    std::mutex m_sleep_lock;
    std::condition_variable m_cv;

    std::unique_ptr<AsyncTask> m_task;
};




}
#endif