/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * @file tcp_connect.cpp Basic functions to create connections without blocking.
 */

#ifdef ENABLE_NETWORK

#include "../../stdafx.h"
#include "../../thread/thread.h"

#include "tcp.h"

/** List of connections that are currently being created */
static SmallVector<TCPConnecter *,  1> _tcp_connecters;

TCPConnecter::TCPConnecter(const NetworkAddress &address) :
	connected(false),
	aborted(false),
	killed(false),
	sock(INVALID_SOCKET),
	address(address)
{
	*_tcp_connecters.Append() = this;
	if (!ThreadObject::New(TCPConnecter::ThreadEntry, this, &this->thread)) {
		this->Connect();
	}
}

void TCPConnecter::Connect()
{
	this->sock = this->address.Connect();
	if (this->sock == INVALID_SOCKET) {
		this->aborted = true;
	} else {
		this->connected = true;
	}
}


/* static */ void TCPConnecter::ThreadEntry(void *param)
{
	static_cast<TCPConnecter*>(param)->Connect();
}

/* static */ void TCPConnecter::CheckCallbacks()
{
	for (TCPConnecter **iter = _tcp_connecters.Begin(); iter < _tcp_connecters.End(); /* nothing */) {
		TCPConnecter *cur = *iter;
		if ((cur->connected || cur->aborted) && cur->killed) {
			_tcp_connecters.Erase(iter);
			if (cur->sock != INVALID_SOCKET) closesocket(cur->sock);
			delete cur;
			continue;
		}
		if (cur->connected) {
			_tcp_connecters.Erase(iter);
			cur->OnConnect(cur->sock);
			delete cur;
			continue;
		}
		if (cur->aborted) {
			_tcp_connecters.Erase(iter);
			cur->OnFailure();
			delete cur;
			continue;
		}
		iter++;
	}
}

/* static */ void TCPConnecter::KillAll()
{
	for (TCPConnecter **iter = _tcp_connecters.Begin(); iter != _tcp_connecters.End(); iter++) (*iter)->killed = true;
}

#endif /* ENABLE_NETWORK */
