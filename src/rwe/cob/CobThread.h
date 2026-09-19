#pragma once

#include <rwe/cob/CobFunction.h>
#include <stack>
#include <string>
#include <vector>

namespace rwe
{
    class CobThread
    {
    public:
        std::string name;

        std::stack<int> stack;

        unsigned int signalMask{0};

        std::stack<CobFunction> callStack;

        /**
         * What the script answered, zero until it answers. It had no
         * initialiser until 2026-09-18, and a thread does not always get as
         * far as a return: an aim script killed by its own signal when the
         * next aim starts is reaped with whatever the heap held, and the
         * weapon code reads that as yes or no. One run in four of a replay
         * fired a shot the recording had not, and nothing after it matched
         * -- units shooting at nothing, factories jammed, both computer
         * players apparently frozen. Zero is also what the original starts
         * from (0x49D580 finds the answer still zero).
         */
        int returnValue{0};

        /**
         * Required for query functions, which communicate back to the engine
         * not by a return value but by changing the values of their input parameters.
         */
        std::vector<int> returnLocals;

    public:
        CobThread(const std::string& name, unsigned int signalMask);

        explicit CobThread(const std::string& name);
    };
}
