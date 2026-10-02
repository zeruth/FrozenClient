#ifndef UI_GAME_C_G_CHARACTER_MODEL_BASE_HPP
#define UI_GAME_C_G_CHARACTER_MODEL_BASE_HPP

#include "ui/simple/CSimpleModel.hpp"
#include "util/guid/Types.hpp"

class CGUnit_C;
struct CreatureCacheRec;

class CGCharacterModelBase : public CSimpleModel {
    public:
        // Static variables
        static int32_t s_metatable;
        static int32_t s_objectType;

        // Static functions
        static CSimpleFrame* Create(CSimpleFrame* parent);
        static void CreateScriptMetaTable();
        static int32_t GetObjectType();

        // Without this the frame answers only to its base's types, and every one of its own
        // script methods fails the This() check with "Wrong object type for member function" --
        // the methods are registered and reachable, the object just denies being what it is.
        virtual bool IsA(int32_t type);
        static void RegisterScriptMethods(lua_State* L);

        // Member variables
        // +0x368: the unit the frame shows, rebuilt from the object manager when the frame shows.
        WOWGUID m_unitGUID = 0;
        // +0x370 / +0x374: SetRotation raises the first and sets the second to now + 100ms; the
        // update drops the turn animation once the deadline passes without another turn.
        int32_t m_turning = 0;
        uint32_t m_turnEnd = 0;
        // +0x378: the creature template the frame shows when it shows no unit.
        const CreatureCacheRec* m_creature = nullptr;

        // Virtual member functions
        virtual int32_t GetScriptMetaTable();
        virtual void OnLayerShow();
        virtual void OnLayerUpdate(float elapsedSec);
        virtual CM2Model* CreateModelFromUnit(CGUnit_C* unit);

        // Member functions
        CGCharacterModelBase(CSimpleFrame* parent);
        void SetCreature(const CreatureCacheRec* creature);
        void SetCreatureModel(const CreatureCacheRec* creature);
        void SetRotation(float rotation);
        void SetUnit(WOWGUID guid);
        void SetUnitModel(CGUnit_C* unit);
};

#endif
