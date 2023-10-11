#include <siberix/proc/sched.h>

namespace siberix::hal {

    using namespace siberix::proc::scheduling;

    class LogicalProcessingUnit
    {
    public:
        LogicalProcessingUnit(UInt16 cpuId, ThreadQueue* threadQueue);
        ~LogicalProcessingUnit();

        unsigned     getId() { return m_cpuId; }
        ThreadQueue* getThreadQueue() { return m_threadQueue; }

    private:
        UInt16       m_cpuId;
        ThreadQueue* m_threadQueue;
        Thread*      m_currentThread;
        Thread*      m_idleThread;
    };

}