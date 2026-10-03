#include "net/connection/ClientConnection.hpp"
#include "net/Login.hpp"
#include "client/ClientServices.hpp"
#include "ui/FrameScript.hpp"
#include "client/Client.hpp"
#include "ui/game/CGGameUI.hpp"
#include "ui/game/Types.hpp"
#include "net/Types.hpp"
#include <storm/Error.hpp>
#include <common/DataStore.hpp>

void ClientConnection::AccountLogin(const char* name, const char* password, int32_t region, WOW_LOCALE locale) {
    // Assertion-like thing

    this->Initiate(COP_AUTHENTICATE, 11, nullptr);
}

void ClientConnection::AccountLogin_Finish(int32_t errorCode) {
    this->Complete(errorCode == 12, errorCode);
}

void ClientConnection::AccountLogin_Queued() {
    this->Initiate(COP_WAIT_QUEUE, 27, nullptr);

    // TODO CGlueMgr::UpdateWaitQueue(this->m_queuePosition);
}

// ref: FUN_006b1200
void ClientConnection::Cancel(int32_t errorCode) {
    this->Complete(0, errorCode);
}

// ref: FUN_006b18c0
void ClientConnection::CancelLogout() {
    CDataStore msg;

    msg.Put(static_cast<uint32_t>(CMSG_LOGOUT_CANCEL));

    msg.Finalize();

    this->Send(&msg);

    this->m_logoutPending = 0;
}

// ref: FUN_006b1790
void ClientConnection::CharacterLogin(uint64_t guid, int32_t a2) {
    this->Initiate(COP_LOGIN_CHARACTER, 76, nullptr);

    if (this->m_connected) {
        this->RequestCharacterLogin(guid, a2);
        this->m_loginRequested = 1;
    } else {
        this->Cancel(4);
    }
}

void ClientConnection::Cleanup() {
    if (this->m_cleanup) {
        this->m_cleanup();
        this->m_cleanup = nullptr;
    }
}

void ClientConnection::Complete(int32_t result, int32_t errorCode) {
    this->Cleanup();

    this->m_statusResult = result;
    this->m_errorCode = errorCode;
    this->m_statusComplete = 1;

    // TODO LogConnectionStatus(this->m_statusCop, errorCode, 0);
}

void ClientConnection::Connect() {
    // TODO

    this->byte2F5A = 0;

    this->Initiate(COP_CONNECT, 7, nullptr);

    if (this->m_connected) {
        this->Complete(1, 5);
    }

    ClientServices::LoginConnection()->GetRealmList();
}

// 0x6B1620 in the original
void ClientConnection::CreateCharacter(const CHARACTER_CREATE_INFO* info) {
    this->Initiate(COP_CREATE_CHARACTER, 46, nullptr);

    if (this->m_connected) {
        this->RequestCharacterCreate(info);
    } else {
        this->Cancel(4);
    }
}

// 0x6B1A70 in the original
void ClientConnection::DeleteCharacter(uint64_t guid) {
    this->Initiate(COP_DELETE_CHARACTER, 70, nullptr);

    if (this->m_connected) {
        this->RequestCharacterDelete(guid);
    } else {
        this->Cancel(4);
    }
}

int32_t ClientConnection::Disconnect() {
    this->NetClient::Disconnect();

    this->m_connected = 0;

    // TODO
    // WardenClient_Destroy();

    return 1;
}

void ClientConnection::EnumerateCharacters(ENUMERATE_CHARACTERS_CALLBACK callback, void* param) {
    // TODO Assertion-like thing

    for (uint32_t i = 0; i < this->m_characterList.Count(); i++) {
        callback(this->m_characterList[i], param);
    }
}

// ref: FUN_006b21f0
void ClientConnection::ForceLogout() {
    this->RequestLogout(0, 1);
}

void ClientConnection::GetCharacterList() {
    this->Initiate(COP_GET_CHARACTERS, 43, nullptr);

    if (this->m_connected) {
        this->RequestCharacterEnum();
    } else {
        this->Cancel(4);
    }
}

void ClientConnection::GetRealmList() {
    this->Initiate(COP_GET_REALMS, 35, nullptr);

    if (ClientServices::LoginConnection()->IsLoggedOn()) {
        ClientServices::LoginConnection()->GetRealmList();
    }
    else {
        ClientServices::LoginConnection()->Reconnect();
    }
}

void ClientConnection::HandleCharacterCreate(uint8_t result) {
    this->Complete(result == 47, result);
}

void ClientConnection::HandleCharacterDelete(uint8_t result) {
    this->Complete(result == 71, result);
}

// ref: FUN_006b2070
// The server would not put the character in the world: leave the game and fail the login with
// the matching CHAR_LOGIN_* code.
void ClientConnection::HandleCharacterLoginFailed(uint8_t result) {
    if (this->m_inWorld) {
        this->SetInWorld(0);
    }

    this->m_loginRequested = 0;

    STORM_ASSERT(ClientServices::s_currentConnection);

    if (this == ClientServices::s_currentConnection) {
        ClientDestroyGame(1, 1, 1);
    }

    switch (result) {
        case 1:
            this->Cancel(78);
            break;

        case 2:
            this->Cancel(79);
            break;

        case 3:
            this->Cancel(80);
            break;

        case 4:
            this->Cancel(82);
            break;

        case 5:
            this->Cancel(83);
            break;

        case 6:
            this->Cancel(84);
            break;

        case 7:
            this->Cancel(85);
            break;

        case 8:
            this->Cancel(86);
            break;

        default:
            this->Cancel(81);
            break;
    }
}

// ref: FUN_006b2180
// The server has taken the character out of the world: back to character select, or out of the
// program when the logout was a quit.
void ClientConnection::HandleLogoutComplete() {
    if (this->m_inWorld) {
        this->SetInWorld(0);
    }

    this->m_loginRequested = 0;

    STORM_ASSERT(ClientServices::s_currentConnection);

    if (this == ClientServices::s_currentConnection) {
        ClientDestroyGame(1, 1, 0);
    }

    this->m_logoutPending = 0;

    if (this->m_logoutQuit) {
        ClientPostClose(0);
    }
}

// ref: FUN_006b08b0
void ClientConnection::HandleLogoutResponse(int32_t result, uint8_t instant) {
    if (result) {
        CGGameUI::DisplayError(410);
        this->m_logoutPending = 0;
    } else if (!instant) {
        FrameScript_SignalEvent(this->m_logoutQuit ? SCRIPT_PLAYER_QUITING : SCRIPT_PLAYER_CAMPING, nullptr);
    }
}

// ref: FUN_006b0900
void ClientConnection::HandleLogoutCancelAck() {
    if (this->m_logoutPending) {
        FrameScript_SignalEvent(SCRIPT_LOGOUT_CANCEL, nullptr);
        this->m_logoutPending = 0;
    }
}

int32_t ClientConnection::HandleConnect() {
    this->Complete(1, 5);

    this->m_connected = 1;

    // TODO WardenClient_Initialize();

    return this->NetClient::HandleConnect();
}

void ClientConnection::Initiate(WOWCS_OPS op, int32_t errorCode, void (*cleanup)()) {
    this->m_cleanup = cleanup;
    this->m_statusCop = op;
    this->m_errorCode = errorCode;
    this->m_statusComplete = 0;

    // TODO LogConnectionStatus(this->m_statusCop, errorCode, 1);
}

int32_t ClientConnection::IsConnected() {
    return this->m_connected;
}

// ref: FUN_006b1930
// Asks the server to take the character out of the world. A request already pending is not
// repeated unless this one is instant, and with no character in the world there is nothing to do.
void ClientConnection::RequestLogout(uint8_t quit, uint8_t instant) {
    if (this->m_logoutPending && !instant) {
        return;
    }

    if (!this->m_inWorld) {
        return;
    }

    STORM_ASSERT(ClientServices::s_currentConnection);

    if (this == ClientServices::s_currentConnection) {
        this->m_logoutQuit = quit;

        if (!instant) {
            this->m_logoutPending = 1;
        }

        CDataStore msg;

        msg.Put(static_cast<uint32_t>(instant ? CMSG_PLAYER_LOGOUT : CMSG_LOGOUT_REQUEST));

        msg.Finalize();

        this->Send(&msg);
    } else {
        this->SetInWorld(0);
        this->m_loginRequested = 0;
    }
}

// ref: FUN_006b1840
// Finishes the character login and records whether a character is now in the world. Leaving the
// world this way also ends any logout that was waiting on the server.
void ClientConnection::SetInWorld(int32_t inWorld) {
    this->Complete(1, 77);

    this->m_inWorld = inWorld;

    if (this->m_logoutPending) {
        FrameScript_SignalEvent(SCRIPT_LOGOUT_CANCEL, nullptr);
        this->m_logoutPending = 0;
    }
}

int32_t ClientConnection::PollStatus(WOWCS_OPS& op, const char** msg, int32_t& result, int32_t& errorCode) {
    op = this->m_statusCop;
    errorCode = this->m_errorCode;
    result = this->m_statusResult;

    static char altText[256];

    if (this->m_statusComplete) {
        auto text = errorCode >= 0 && errorCode < 104
            ? FrameScript_GetText(ClientServices::GetErrorToken(errorCode), -1, GENDER_NOT_APPLICABLE)
            : nullptr;

        if (!text) {
            SStrPrintf(altText, sizeof(altText), "(%i)", errorCode);
            *msg = altText;
        } else if (errorCode == 27) {
            // TODO
            *msg = "TODO";
        } else {
            *msg = text;
        }

        return this->m_statusComplete;
    }

    *msg = "";

    return this->m_statusComplete;
}
