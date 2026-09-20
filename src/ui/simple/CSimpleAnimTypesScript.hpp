#ifndef UI_SIMPLE_C_SIMPLE_ANIM_TYPES_SCRIPT_HPP
#define UI_SIMPLE_C_SIMPLE_ANIM_TYPES_SCRIPT_HPP

#include "ui/FrameScript.hpp"

// Counts and orderings read out of the reference's tables. They sit in class order with no gaps
// between Animation at 00ac18e0 and AnimationGroup at 00ac1ab8:
//
//     00ac19dc  Translation       2
//     00ac19f0  Rotation          6
//     00ac1a24  Scale             4
//     00ac1a48  ControlPoint      5
//     00ac1a74  Path              5
//     00ac1aa0  Alpha             2
//
// None of these six carries GetObjectType / IsObjectType / GetName / GetParent, the same way
// Animation does not and AnimationGroup does.
#define NUM_SIMPLE_TRANSLATION_ANIM_SCRIPT_METHODS 2
#define NUM_SIMPLE_ROTATION_ANIM_SCRIPT_METHODS 6
#define NUM_SIMPLE_SCALE_ANIM_SCRIPT_METHODS 4
#define NUM_SIMPLE_CONTROL_POINT_SCRIPT_METHODS 5
#define NUM_SIMPLE_PATH_ANIM_SCRIPT_METHODS 5
#define NUM_SIMPLE_ALPHA_ANIM_SCRIPT_METHODS 2

extern FrameScript_Method SimpleTranslationAnimMethods[NUM_SIMPLE_TRANSLATION_ANIM_SCRIPT_METHODS];
extern FrameScript_Method SimpleRotationAnimMethods[NUM_SIMPLE_ROTATION_ANIM_SCRIPT_METHODS];
extern FrameScript_Method SimpleScaleAnimMethods[NUM_SIMPLE_SCALE_ANIM_SCRIPT_METHODS];
extern FrameScript_Method SimpleControlPointMethods[NUM_SIMPLE_CONTROL_POINT_SCRIPT_METHODS];
extern FrameScript_Method SimplePathAnimMethods[NUM_SIMPLE_PATH_ANIM_SCRIPT_METHODS];
extern FrameScript_Method SimpleAlphaAnimMethods[NUM_SIMPLE_ALPHA_ANIM_SCRIPT_METHODS];

#endif
