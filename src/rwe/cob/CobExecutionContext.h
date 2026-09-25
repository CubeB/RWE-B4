#pragma once

#include <rwe/cob/CobAngularSpeed.h>
#include <rwe/cob/CobEnvironment.h>
#include <rwe/cob/CobPosition.h>
#include <rwe/cob/CobSfxType.h>
#include <rwe/cob/CobSleepDuration.h>
#include <rwe/cob/CobSpeed.h>
#include <rwe/cob/CobValueId.h>

namespace rwe
{
    class CobExecutionContext
    {
    private:
        CobEnvironment* const env;
        CobThread* const thread;

    public:
        /**
         * Limits on what one script thread may do, none of which a script
         * the original would run comes near. Past any of them execute()
         * throws, and the scheduler kills the thread (sim/cob.cpp), as the
         * original kills one that meets an opcode it does not know. Before
         * these, a mod's script could ask for a sixteen-gigabyte argument
         * list, recurse or push until memory ran out, or loop without ever
         * yielding, and every peer of a network game hung or died together.
         * Issue #75.
         *
         * The original gives a thread a 32-word window, so no function has
         * more than that many arguments; the rest are generous multiples of
         * anything a shipped script does between sleeps.
         */
        static constexpr unsigned int MaxScriptParams = 64;
        static constexpr std::size_t MaxCallDepth = 256;
        static constexpr std::size_t MaxStackDepth = 4096;
        static constexpr unsigned int MaxInstructionsPerRun = 1000000;
        static constexpr std::size_t MaxThreadsPerUnit = 256;

        CobExecutionContext(CobEnvironment* env, CobThread* thread);

        CobEnvironment::Status execute();

    private:
        unsigned int checkedParamCount(unsigned int paramCount) const;

        unsigned int checkedPiece(unsigned int piece) const;

        // arithmetic
        void add();

        void subtract();

        void multiply();

        void divide();

        // comparision
        void compareLessThan();

        void compareLessThanOrEqual();

        void compareEqual();

        void compareNotEqual();

        void compareGreaterThan();

        void compareGreaterThanOrEqual();

        // control flow
        void jump();

        void jumpIfZero();

        // boolean logic
        void logicalAnd();

        void logicalOr();

        void logicalXor();

        void logicalNot();

        // bitwise logic
        void bitwiseAnd();

        void bitwiseOr();

        void bitwiseXor();

        void bitwiseNot();



        // script dispatch and return
        void returnFromScript();

        void callScript();

        void startScript();

        // signalling

        void setSignalMask();

        // variables
        void createLocalVariable();

        void pushConstant();

        void pushLocalVariable();

        void popLocalVariable();

        void pushStaticVariable();

        void popStaticVariable();

        void popStackOperation();

        CobEnvironment::SetQueryStatus setValue();

        // non-commands
        int pop();

        CobSleepDuration popSleepDuration();
        CobPosition popPosition();
        CobSpeed popSpeed();
        CobAngle popAngle();
        CobAngularSpeed popAngularSpeed();
        unsigned int popSignal();
        unsigned int popSignalMask();
        CobValueId popValueId();
        CobSfxType popSfxType();
        void push(int val);

        unsigned int nextInstruction();
        CobAxis nextInstructionAsAxis();
    };
}
