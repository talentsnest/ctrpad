// Network pads: phones as controllers (talentsnest/manette-tel).
//
// The phone page talks to a small server on the Mac, which forwards each press as RetroArch's
// "Network RetroPad" protocol: one UDP port per player (55400 + player index), each datagram being
// RetroArch's `struct remote_message { int port; int device; int index; int id; uint16_t state; }`
// (20 bytes, host byte order). The same server therefore drives RetroArch and CTRPad.
//
// Each network player becomes an SDL virtual gamepad, attached on that player's first datagram.
// The existing SDL gamepad path then owns everything else: slot assignment, the multitap bus,
// the PlayStation mapping and analog. Nothing here touches retail pad bytes directly.
//
// Desktop only (macOS, Linux). Disable with CTR_NETPAD=0; move the ports with CTR_NETPAD_PORT.
// Sockets listen on loopback only: the phone server runs on the same machine.

#include <macros.h>
#include "platform/native_log.h"

#include <SDL3/SDL.h>
#include <stdlib.h>
#include <string.h>

#if !defined(_WIN32) && !defined(SDL_PLATFORM_IOS)
#define NATIVE_NETPAD_ENABLED 1
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#else
#define NATIVE_NETPAD_ENABLED 0
#endif

#define NATIVE_NETPAD_PLAYERS        4
#define NATIVE_NETPAD_BASE_PORT      55400
#define NATIVE_NETPAD_MESSAGE_BYTES  20
#define NATIVE_NETPAD_DEVICE_JOYPAD  1
#define NATIVE_NETPAD_DEVICE_ANALOG  5
#define NATIVE_NETPAD_DRAIN_MAX      64

struct NativeNetpadPlayer
{
	int fd;
	SDL_JoystickID instanceId;
	SDL_Joystick *joystick;
};

global_variable struct NativeNetpadPlayer s_netpadPlayers[NATIVE_NETPAD_PLAYERS];
global_variable s32 s_netpadActive;

void Platform_InputControllerAdded(int deviceIndex);

// RetroPad button id (0-15) -> SDL gamepad button, or -1 for the two triggers (axes) and unused ids.
// Face buttons by position: RetroPad B/Y/A/X = south/west/east/north = Cross/Square/Circle/Triangle.
internal s32 NativeNetpad_ButtonForRetroPadId(s32 id)
{
	switch (id)
	{
	case 0: return SDL_GAMEPAD_BUTTON_SOUTH;
	case 1: return SDL_GAMEPAD_BUTTON_WEST;
	case 2: return SDL_GAMEPAD_BUTTON_BACK;
	case 3: return SDL_GAMEPAD_BUTTON_START;
	case 4: return SDL_GAMEPAD_BUTTON_DPAD_UP;
	case 5: return SDL_GAMEPAD_BUTTON_DPAD_DOWN;
	case 6: return SDL_GAMEPAD_BUTTON_DPAD_LEFT;
	case 7: return SDL_GAMEPAD_BUTTON_DPAD_RIGHT;
	case 8: return SDL_GAMEPAD_BUTTON_EAST;
	case 9: return SDL_GAMEPAD_BUTTON_NORTH;
	case 10: return SDL_GAMEPAD_BUTTON_LEFT_SHOULDER;
	case 11: return SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER;
	case 14: return SDL_GAMEPAD_BUTTON_LEFT_STICK;
	case 15: return SDL_GAMEPAD_BUTTON_RIGHT_STICK;
	default: return -1;
	}
}

internal s32 NativeNetpad_Attach(s32 player)
{
	static const char *names[NATIVE_NETPAD_PLAYERS] = {
		"Phone pad 1", "Phone pad 2", "Phone pad 3", "Phone pad 4",
	};
	struct NativeNetpadPlayer *pad = &s_netpadPlayers[player];
	SDL_VirtualJoystickDesc desc;

	if (pad->joystick != NULL)
	{
		return 1;
	}

	SDL_INIT_INTERFACE(&desc);
	desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
	desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
	desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
	desc.name = names[player];

	pad->instanceId = SDL_AttachVirtualJoystick(&desc);
	if (pad->instanceId == 0)
	{
		Platform_Log("[CTR Netpad] attach player=%d failed: %s\n", player + 1, SDL_GetError());
		return 0;
	}

	pad->joystick = SDL_OpenJoystick(pad->instanceId);
	if (pad->joystick == NULL)
	{
		SDL_DetachVirtualJoystick(pad->instanceId);
		pad->instanceId = 0;
		return 0;
	}

	// The SDL_EVENT_GAMEPAD_ADDED that follows is harmless: an already-owned instance keeps its slot.
	Platform_InputControllerAdded((int)pad->instanceId);
	Platform_Log("[CTR Netpad] player=%d attached instance=%u\n", player + 1, (unsigned int)pad->instanceId);
	return 1;
}

internal void NativeNetpad_Apply(s32 player, const u8 *msg)
{
	s32 device;
	s32 index;
	s32 id;
	s16 state;
	s32 button;
	struct NativeNetpadPlayer *pad;

	memcpy(&device, msg + 4, sizeof(device));
	memcpy(&index, msg + 8, sizeof(index));
	memcpy(&id, msg + 12, sizeof(id));
	memcpy(&state, msg + 16, sizeof(state));

	// Players attach in order (the phone server numbers them without gaps), so a lower phone
	// never ends up on a higher pad slot because it pressed later.
	for (s32 p = 0; p <= player; p++)
	{
		if (!NativeNetpad_Attach(p))
		{
			return;
		}
	}
	pad = &s_netpadPlayers[player];

	if (device == NATIVE_NETPAD_DEVICE_JOYPAD)
	{
		if ((id == 12) || (id == 13))
		{
			SDL_SetJoystickVirtualAxis(pad->joystick, id == 12 ? SDL_GAMEPAD_AXIS_LEFT_TRIGGER : SDL_GAMEPAD_AXIS_RIGHT_TRIGGER,
			                           state ? 32767 : 0);
			return;
		}
		button = NativeNetpad_ButtonForRetroPadId(id);
		if (button >= 0)
		{
			SDL_SetJoystickVirtualButton(pad->joystick, button, state != 0);
		}
	}
	else if ((device == NATIVE_NETPAD_DEVICE_ANALOG) && (index >= 0) && (index <= 1) && (id >= 0) && (id <= 1))
	{
		static const s32 axes[2][2] = {
			{SDL_GAMEPAD_AXIS_LEFTX, SDL_GAMEPAD_AXIS_LEFTY},
			{SDL_GAMEPAD_AXIS_RIGHTX, SDL_GAMEPAD_AXIS_RIGHTY},
		};
		SDL_SetJoystickVirtualAxis(pad->joystick, axes[index][id], state);
	}
}

internal void NativeNetpad_Open(s32 basePort)
{
#if NATIVE_NETPAD_ENABLED
	s32 opened = 0;

	for (s32 p = 0; p < NATIVE_NETPAD_PLAYERS; p++)
	{
		s_netpadPlayers[p].fd = -1;
		s_netpadPlayers[p].instanceId = 0;
		s_netpadPlayers[p].joystick = NULL;
	}

	for (s32 p = 0; p < NATIVE_NETPAD_PLAYERS; p++)
	{
		struct sockaddr_in addr;
		int one = 1;
		int fd = socket(AF_INET, SOCK_DGRAM, 0);

		if (fd < 0)
		{
			continue;
		}
		setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
		memset(&addr, 0, sizeof(addr));
		addr.sin_family = AF_INET;
		addr.sin_port = htons((u16)(basePort + p));
		addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		if ((bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) || (fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK) != 0))
		{
			Platform_Log("[CTR Netpad] port %d unavailable (RetroArch open?): %s\n", basePort + p, strerror(errno));
			close(fd);
			continue;
		}
		s_netpadPlayers[p].fd = fd;
		opened++;
	}

	s_netpadActive = opened > 0;
	Platform_Log("[CTR Netpad] listening on 127.0.0.1:%d-%d (%d of %d players)\n", basePort, basePort + NATIVE_NETPAD_PLAYERS - 1,
	             opened, NATIVE_NETPAD_PLAYERS);
#endif
}

internal void NativeNetpad_Init(void)
{
#if NATIVE_NETPAD_ENABLED
	const char *env = SDL_getenv("CTR_NETPAD");
	const char *portEnv = SDL_getenv("CTR_NETPAD_PORT");
	s32 basePort = NATIVE_NETPAD_BASE_PORT;

	if ((env != NULL) && (strcmp(env, "0") == 0))
	{
		return;
	}
	if ((portEnv != NULL) && (atoi(portEnv) > 0) && (atoi(portEnv) < 65532))
	{
		basePort = atoi(portEnv);
	}
	NativeNetpad_Open(basePort);
#endif
}

internal void NativeNetpad_Poll(void)
{
#if NATIVE_NETPAD_ENABLED
	u8 msg[NATIVE_NETPAD_MESSAGE_BYTES + 4];

	if (s_netpadActive == 0)
	{
		return;
	}

	for (s32 p = 0; p < NATIVE_NETPAD_PLAYERS; p++)
	{
		if (s_netpadPlayers[p].fd < 0)
		{
			continue;
		}
		for (s32 n = 0; n < NATIVE_NETPAD_DRAIN_MAX; n++)
		{
			ssize_t got = recv(s_netpadPlayers[p].fd, msg, sizeof(msg), 0);

			if (got < 0)
			{
				break; // EAGAIN: nothing queued (or a socket error: keep the last state)
			}
			if (got == NATIVE_NETPAD_MESSAGE_BYTES)
			{
				NativeNetpad_Apply(p, msg);
			}
		}
	}
#endif
}

internal void NativeNetpad_Shutdown(void)
{
#if NATIVE_NETPAD_ENABLED
	for (s32 p = 0; p < NATIVE_NETPAD_PLAYERS; p++)
	{
		struct NativeNetpadPlayer *pad = &s_netpadPlayers[p];

		if (pad->joystick != NULL)
		{
			SDL_CloseJoystick(pad->joystick);
			pad->joystick = NULL;
		}
		if (pad->instanceId != 0)
		{
			SDL_DetachVirtualJoystick(pad->instanceId);
			pad->instanceId = 0;
		}
		if (pad->fd >= 0)
		{
			close(pad->fd);
			pad->fd = -1;
		}
	}
	s_netpadActive = 0;
#endif
}
