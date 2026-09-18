#pragma once

#include <optional>
#include <rwe/sim/SimAngle.h>
#include <rwe/sim/SimScalar.h>
#include <rwe/sim/SimVector.h>
#include <variant>


namespace rwe
{
    struct UnitMesh
    {
        struct MoveOperation
        {
            SimScalar targetPosition;
            SimScalar speed;

            MoveOperation(SimScalar targetPosition, SimScalar speed);
        };

        struct TurnOperation
        {
            SimAngle targetAngle;
            SimScalar speed;

            TurnOperation(SimAngle targetAngle, SimScalar speed);
        };

        struct SpinOperation
        {
            SimScalar currentSpeed;
            SimScalar targetSpeed;
            SimScalar acceleration;

            SpinOperation(SimScalar currentSpeed, SimScalar targetSpeed, SimScalar acceleration)
                : currentSpeed(currentSpeed),
                  targetSpeed(targetSpeed),
                  acceleration(acceleration)
            {
            }
        };

        struct StopSpinOperation
        {
            SimScalar currentSpeed;
            SimScalar deceleration;

            StopSpinOperation(SimScalar currentSpeed, SimScalar deceleration)
                : currentSpeed(currentSpeed),
                  deceleration(deceleration)
            {
            }
        };

        using TurnOperationUnion = std::variant<TurnOperation, SpinOperation, StopSpinOperation>;

        std::string name;
        bool visible{true};
        bool shaded{true};
        /**
         * The COB CACHE / DONT_CACHE state. The original keeps a finished
         * unit in a cached bitmap rendered by the shaded rasterizer, and a
         * piece the script has marked dont-cache is left out of it and drawn
         * straight to the screen each frame by the unshaded twin
         * (TOTALA-EXE-SHADING.md S:12a, S:15 item 8). So, once the unit is
         * finished, a dont-cache piece is not shaded. 137 of the shipped
         * scripts use it, mostly on the pieces that move: turrets, lab arms,
         * radar dishes. Like `shaded`, this is render-facing state that is
         * saved but not hashed.
         */
        bool cached{true};
        SimVector offset{0_ss, 0_ss, 0_ss};
        SimVector previousOffset{0_ss, 0_ss, 0_ss};
        SimAngle previousRotationX{0};
        SimAngle previousRotationY{0};
        SimAngle previousRotationZ{0};
        SimAngle rotationX{0};
        SimAngle rotationY{0};
        SimAngle rotationZ{0};

        std::optional<MoveOperation> xMoveOperation;
        std::optional<MoveOperation> yMoveOperation;
        std::optional<MoveOperation> zMoveOperation;

        std::optional<TurnOperationUnion> xTurnOperation;
        std::optional<TurnOperationUnion> yTurnOperation;
        std::optional<TurnOperationUnion> zTurnOperation;

        void update(SimScalar dt);
    };
}
