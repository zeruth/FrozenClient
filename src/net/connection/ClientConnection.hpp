#ifndef NET_CONNECTION_CLIENT_CONNECTION_HPP
#define NET_CONNECTION_CLIENT_CONNECTION_HPP

#include "net/connection/RealmConnection.hpp"
#include "net/Types.hpp"
#include "util/Locale.hpp"

class RealmResponse;

typedef void (*ENUMERATE_CHARACTERS_CALLBACK)(const CHARACTER_INFO&, void*);

class ClientConnection : public RealmConnection {
    public:
        // Member variables
        int32_t m_connected = 0;
        // Set once the character login request has gone out; a logout or a failed login clears it.
        int32_t m_loginRequested = 0;
        int32_t m_statusComplete = 1;
        int32_t m_statusResult = 1;
        WOWCS_OPS m_statusCop = COP_NONE;
        int32_t m_errorCode = 0;
        // A character is in the world: logging out is only possible while this is set.
        int32_t m_inWorld = 0;
        // The pending logout ends the program rather than returning to character select.
        uint8_t m_logoutQuit = 0;
        // A logout has been asked for and the server has not yet finished or cancelled it.
        uint8_t m_logoutPending = 0;
        uint8_t byte2F5A = 0;
        void (*m_cleanup)() = nullptr;

        // Virtual member functions
        virtual int32_t HandleConnect();
        virtual void HandleCharacterCreate(uint8_t result);
        virtual void HandleCharacterDelete(uint8_t result);
        virtual void HandleCharacterLoginFailed(uint8_t result);
        virtual void HandleLogoutComplete();
        virtual void HandleLogoutResponse(int32_t result, uint8_t instant);
        virtual void HandleLogoutCancelAck();

        // Member functions
        ClientConnection(RealmResponse* realmResponse)
            : RealmConnection(realmResponse)
            {};
        void AccountLogin(const char* name, const char* password, int32_t region, WOW_LOCALE locale);
        void AccountLogin_Finish(int32_t authResult);
        void AccountLogin_Queued();
        void Cancel(int32_t errorCode);
        void CancelLogout();
        void CharacterLogin(uint64_t guid, int32_t a2);
        void Cleanup();
        void Complete(int32_t result, int32_t errorCode);
        void Connect();
        void CreateCharacter(const CHARACTER_CREATE_INFO* info);
        void DeleteCharacter(uint64_t guid);
        int32_t Disconnect();
        void EnumerateCharacters(ENUMERATE_CHARACTERS_CALLBACK callback, void* param);
        void ForceLogout();
        void GetCharacterList();
        void GetRealmList();
        void Initiate(WOWCS_OPS op, int32_t errorCode, void (*cleanup)());
        int32_t IsConnected();
        int32_t PollStatus(WOWCS_OPS& op, const char** msg, int32_t& result, int32_t& errorCode);
        void RequestLogout(uint8_t quit, uint8_t instant);
        void SetInWorld(int32_t inWorld);
};

#endif
