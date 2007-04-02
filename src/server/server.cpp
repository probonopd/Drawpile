/*******************************************************************************

   Copyright (C) 2006, 2007 M.K.A. <wyrmchild@users.sourceforge.net>

   Permission is hereby granted, free of charge, to any person obtaining a
   copy of this software and associated documentation files (the "Software"),
   to deal in the Software without restriction, including without limitation
   the rights to use, copy, modify, merge, publish, distribute, sublicense,
   and/or sell copies of the Software, and to permit persons to whom the
   Software is furnished to do so, subject to the following conditions:
   
   Except as contained in this notice, the name(s) of the above copyright
   holders shall not be used in advertising or otherwise to promote the sale,
   use or other dealings in this Software without prior written authorization.
   
   The above copyright notice and this permission notice shall be included in
   all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
   AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
   LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
   FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
   DEALINGS IN THE SOFTWARE.

   ---

   For more info, see: http://drawpile.sourceforge.net/

*******************************************************************************/

#include "server.h"

#include "../shared/templates.h"
#include "../shared/protocol.types.h"
#include "../shared/protocol.defaults.h"
#include "../shared/protocol.helper.h"
#include "../shared/protocol.h" // Message()

#include "server.flags.h"

#if defined(HAVE_ZLIB)
	#include <zlib.h>
#endif

#include <ctime>
#include <getopt.h> // for command-line opts
#include <cstdlib>
#include <iostream>

#include <set>
#include <vector>
#include <list>

/* iterators */
typedef std::map<uint8_t, Session*>::iterator sessionmap_iterator;
typedef std::map<uint8_t, Session*>::const_iterator sessionmap_const_iterator;
typedef std::map<fd_t, User*>::iterator usermap_iterator;
typedef std::map<fd_t, User*>::const_iterator usermap_const_iterator;
typedef std::multimap<fd_t, fd_t>::iterator tunnelmap_iterator;
typedef std::multimap<fd_t, fd_t>::const_iterator tunnelmap_const_iterator;
typedef std::list<User*>::iterator userlist_iterator;
typedef std::list<User*>::const_iterator userlist_const_iterator;
typedef std::vector<message_ref>::iterator msgvector_iterator;
typedef std::vector<message_ref>::const_iterator msgvector_const_iterator;

Server::Server() throw()
	: state(server::state::None),
	password(0),
	a_password(0),
	pw_len(0),
	a_pw_len(0),
	user_limit(0),
	session_limit(srv_defaults::session_limit),
	max_subscriptions(srv_defaults::max_subscriptions),
	name_len_limit(srv_defaults::name_len_limit),
	time_limit(srv_defaults::time_limit),
	current_time(0),
	next_timer(0),
	hi_port(protocol::default_port),
	lo_port(protocol::default_port),
	min_dimension(srv_defaults::min_dimension),
	requirements(0),
	extensions(
		protocol::extensions::Chat|protocol::extensions::Palette
		#ifdef HAVE_ZLIB
		|protocol::extensions::Deflate
		#endif // HAVE_ZLIB
	),
	default_user_mode(protocol::user_mode::None),
	Transient(false),
	LocalhostAdmin(false)
{
	for (uint8_t i=0; i != std::numeric_limits<uint8_t>::max(); ++i)
	{
		user_ids.push(i+1);
		session_ids.push(i+1);
	}
}

Server::~Server() throw()
{
	cleanup();
}

const uint8_t Server::getUserID() throw()
{
	if (user_ids.empty())
		return protocol::null_user;
	
	const uint8_t n = user_ids.front();
	user_ids.pop();
	
	return n;
}

const uint8_t Server::getSessionID() throw()
{
	if (session_ids.empty())
		return protocol::Global;
	
	const uint8_t n = session_ids.front();
	session_ids.pop();
	
	return n;
}

void Server::freeUserID(const uint8_t id) throw()
{
	assert(id != protocol::null_user);
	
	// unfortunately queue can't be iterated, so we can't test if the ID is valid
	
	user_ids.push(id);
}

void Server::freeSessionID(const uint8_t id) throw()
{
	assert(id != protocol::Global);
	
	// unfortunately queue can't be iterated, so we can't test if the ID is valid
	
	session_ids.push(id);
}

void Server::cleanup() throw()
{
	// finish event system
	ev.finish();
	
	// close listening socket
	lsock.close();
	
	#if defined( FULL_CLEANUP )
	users.clear();
	#endif // FULL_CLEANUP
}

void Server::uRegenSeed(User* usr) const throw()
{
	assert(usr != 0);
	
	usr->seed[0] = (rand() % 255) + 1;
	usr->seed[1] = (rand() % 255) + 1;
	usr->seed[2] = (rand() % 255) + 1;
	usr->seed[3] = (rand() % 255) + 1;
}

inline
message_ref Server::msgAuth(User* usr, const uint8_t session) const throw(std::bad_alloc)
{
	assert(usr != 0);
	
	protocol::Authentication* auth = new protocol::Authentication;
	auth->session_id = session;
	
	uRegenSeed(usr);
	
	memcpy(auth->seed, usr->seed, protocol::password_seed_size);
	
	return message_ref(auth);
}
inline
message_ref Server::msgHostInfo() const throw(std::bad_alloc)
{
	return message_ref(
		new protocol::HostInfo(
			sessions.size(),
			session_limit,
			users.size(),
			user_limit,
			name_len_limit,
			max_subscriptions,
			requirements,
			extensions
		)
	);
}

inline
message_ref Server::msgSessionInfo(Session*& session) const throw(std::bad_alloc)
{
	assert(session != 0);
	
	protocol::SessionInfo *nfo = new protocol::SessionInfo(
		session->width,
		session->height,
		session->owner,
		session->users.size(),
		session->limit,
		session->mode,
		session->getFlags(),
		session->level,
		session->len,
		new char[session->len]
	);
	
	nfo->session_id = session->id;
	
	memcpy(nfo->title, session->title, session->len);
	
	return message_ref(nfo);
}

inline
message_ref Server::msgError(const uint8_t session, const uint16_t code) const throw(std::bad_alloc)
{
	protocol::Error *err = new protocol::Error(code);
	err->session_id = session;
	return message_ref(err);
}

inline
message_ref Server::msgAck(const uint8_t session, const uint8_t type) const throw(std::bad_alloc)
{
	protocol::Acknowledgement *ack = new protocol::Acknowledgement(type);
	ack->session_id = session;
	return message_ref(ack);
}

inline
message_ref Server::msgSyncWait(Session*& session) const throw(std::bad_alloc)
{
	assert(session != 0);
	
	message_ref sync_ref(new protocol::SyncWait);
	sync_ref->session_id = session->id;
	return sync_ref;
}

void Server::uWrite(User*& usr) throw()
{
	assert(usr != 0);
	
	#if defined(DEBUG_SERVER) and !defined(NDEBUG)
	std::cout << "Server::uWrite(user: " << static_cast<int>(usr->id) << ")" << std::endl;
	#endif
	
	// if buffer is empty
	if (usr->output.isEmpty())
	{
		assert(!usr->queue.empty());
		
		const std::deque<message_ref>::iterator f_msg(usr->queue.begin());
		std::deque<message_ref>::iterator l_msg(f_msg+1), iter(f_msg);
		for (int i=1; l_msg != usr->queue.end(); ++l_msg, ++iter, ++i)
		{
			if (i == std::numeric_limits<uint8_t>::max()
				or ((*l_msg)->type != (*f_msg)->type)
				or ((*l_msg)->user_id != (*f_msg)->user_id)
				or ((*l_msg)->session_id != (*f_msg)->session_id)
			)
				break; // type changed or reached maximum size of linked list
			
			// create linked list
			(*l_msg)->prev = boost::get_pointer(*iter);
			(*iter)->next = boost::get_pointer(*l_msg);
		}
		
		//msg->user_id = id;
		size_t len=0, size=usr->output.canWrite();
		
		// serialize message/s
		char* buf = (*--l_msg)->serialize(len, usr->output.wpos, size);
		
		// in case new buffer was allocated
		if (buf != usr->output.wpos)
			usr->output.setBuffer(buf, size);
		
		usr->output.write(len);
		
		// clear linked list...
		/*
		// no longer needed
		for (iter = f_msg; iter != l_msg; ++iter)
			(*iter)->next = (*iter)->prev = 0;
		*/
		
		#if defined(HAVE_ZLIB)
		if (usr->ext_deflate
			and usr->output.canRead() > 300
			and (*f_msg)->type != protocol::type::Raster)
		{
			// TODO: Move to separate function
			
			char* temp;
			unsigned long buffer_len = len + 12;
			// make the potential new buffer generous in its size
			
			bool inBuffer;
			
			if (usr->output.canWrite() < buffer_len)
			{ // can't write continuous stream of data in buffer
				assert(usr->output.free() < buffer_len);
				size = buffer_len*2048+1024;
				temp = new char[size];
				inBuffer = false;
			}
			else
			{
				temp = usr->output.wpos;
				inBuffer = true;
			}
			
			const int r = compress2(reinterpret_cast<unsigned char*>(temp), &buffer_len, reinterpret_cast<unsigned char*>(usr->output.rpos), len, 5);
			
			assert(r != Z_STREAM_ERROR);
			
			switch (r)
			{
			default:
			case Z_OK:
				#ifndef NDEBUG
				std::cout << "zlib: " << len << "B compressed down to " << buffer_len << "B." << std::endl;
				#endif
				
				usr->output.read(len);
				
				if (inBuffer)
				{
					usr->output.write(buffer_len);
					size = usr->output.canWrite();
				}
				else
				{
					usr->output.setBuffer(temp, size);
					usr->output.write(buffer_len);
				}
				
				{
					const protocol::Deflate t_deflate(len, buffer_len, temp);
					
					/*
					if (usr->output.canWrite() < buffer_len + 9)
						if (usr->output.free() >= buffer_len + 9)
						{
							reposition();
							size = usr->output.canWrite();
						}
					*/
					
					buf = t_deflate.serialize(len, usr->output.wpos, size);
					
					if (buf != usr->output.wpos)
					{
						#ifndef NDEBUG
						if (!inBuffer)
						{
							std::cout << "Pre-allocated buffer was too small!" << std::endl
								<< "Allocated: " << buffer_len*2+1024
								<< ", actually needed: " << size << std::endl;
						}
						#endif
						usr->output.setBuffer(buf, size);
					}
					
					usr->output.write(len);
					
					// cleanup
				}
				break;
			case Z_MEM_ERROR:
				throw std::bad_alloc();
			case Z_BUF_ERROR:
				#ifndef NDEBUG
				std::cerr << "zlib: output buffer is too small." << std::endl
					<< "source size: " << len << ", target size: " << buffer_len << std::endl;
				#endif
				assert(r != Z_BUF_ERROR);
				
				if (!inBuffer)
					delete [] temp;
				
				break;
			}
		}
		#endif // HAVE_ZLIB
		
		usr->queue.erase(f_msg, ++l_msg);
	}
	
	const int sb = usr->sock->send(
		usr->output.rpos,
		usr->output.canRead()
	);
	
	if (sb == SOCKET_ERROR)
	{
		switch (usr->sock->getError())
		{
		#ifdef WSA_SOCKET
		case WSA_IO_PENDING:
		case WSAEWOULDBLOCK:
		#endif // WSA_SOCKET
		case EINTR:
		case EAGAIN:
		case ENOBUFS:
		case ENOMEM:
			// retry
			break;
		default:
			std::cerr << "Error occured while sending to user #"
				<< static_cast<int>(usr->id) << std::endl;
			
			uRemove(usr, protocol::user_event::BrokenPipe);
			break;
		}
		
		return;
	}
	else if (sb == 0)
	{
		// no data was sent, and no error occured.
	}
	else
	{
		usr->output.read(sb);
		
		// just to ensure we don't need to do anything for it.
		
		if (usr->output.isEmpty())
		{
			// remove buffer
			// usr->output.rewind();
			
			// remove fd from write list if no buffers left.
			if (usr->queue.empty())
			{
				fClr(usr->events, ev.write);
				assert(usr->events != 0);
				ev.modify(usr->sock->fd(), usr->events);
			}
		}
	}
}

void Server::uRead(User*& usr) throw(std::bad_alloc)
{
	assert(usr != 0);
	
	#if defined(DEBUG_SERVER) and !defined(NDEBUG)
	std::cout << "Server::uRead(user: " << static_cast<int>(usr->id) << ")" << std::endl;
	#endif
	
	if (usr->input.free() == 0)
	{
		// buffer full
		#ifndef NDEBUG
		std::cerr << "Input buffer full, increasing size by 4 kiB." << std::endl;
		#endif
		usr->input.resize(usr->input.size + 4096);
	}
	else if (usr->input.canWrite() == 0)
	{
		// can't write, but buffer isn't full.
		usr->input.reposition();
	}
	
	const int rb = usr->sock->recv(
		usr->input.wpos,
		usr->input.canWrite()
	);
	
	if (rb == SOCKET_ERROR)
	{
		switch (usr->sock->getError())
		{
		#ifdef WSA_SOCKETS
		case WSAEWOULDBLOCK:
		#else
		case EAGAIN:
		#endif
		case EINTR:
			// retry later
			#if defined(DEBUG_SERVER) and !defined(NDEBUG)
			std::cerr << "# Operation would block / interrupted" << std::endl;
			#endif
			return;
		default:
			std::cerr << "Unrecoverable error occured while reading from user #"
				<< static_cast<int>(usr->id) << std::endl;
			
			uRemove(usr, protocol::user_event::BrokenPipe);
			return;
		}
	}
	else if (rb == 0)
	{
		std::cerr << "User disconnected #" << static_cast<int>(usr->id) << std::endl;
		uRemove(usr, protocol::user_event::Disconnect);
		return;
	}
	else
	{
		usr->input.write(rb);
		
		uProcessData(usr);
	}
}

void Server::uProcessData(User*& usr) throw()
{
	assert(usr != 0);
	
	#if defined(DEBUG_SERVER) and !defined(NDEBUG)
	std::cout << "Server::uProcessData(user: "
		<< static_cast<int>(usr->id) << ", in buffer: "
		<< usr->input.left << " bytes)" << std::endl;
	#endif
	
	while (usr->input.canRead() != 0)
	{
		if (usr->inMsg == 0)
		{
			usr->inMsg  = protocol::getMessage(usr->input.rpos[0]);
			if (usr->inMsg == 0)
			{
				// unknown message type
				uRemove(usr, protocol::user_event::Violation);
				return;
			}
		}
		else if (usr->inMsg->type != usr->input.rpos[0])
		{
			// Input buffer frelled
			std::cerr << "Buffer corruption, message type changed." << std::endl;
			uRemove(usr, protocol::user_event::Dropped);
			return;
		}
		
		size_t cread = usr->input.canRead();
		size_t len = usr->inMsg->reqDataLen(usr->input.rpos, cread);
		if (len > usr->input.left)
			return; // need more data
		else if (len > cread)
		{
			// so, we reposition the buffer
			usr->input.reposition();
			
			// update cread and len
			cread = usr->input.canRead();
			len = usr->inMsg->reqDataLen(usr->input.rpos, cread);
		}
		
		usr->input.read(
			usr->inMsg->unserialize(usr->input.rpos, cread)
		);
		
		/*
		// isValid() can't be used before it is properly implemented!
		if (!usr->inMsg->isValid())
		{
			std::cerr << "Message is invalid, dropping user." << std::endl;
			uRemove(usr, protocol::user_event::Dropped);
			return;
		}
		*/
		
		switch (usr->state)
		{
		case uState::active:
			uHandleMsg(usr);
			break;
		default:
			uHandleLogin(usr);
			break;
		}
		
		if (usr != 0)
		{ // user is still alive.
			delete usr->inMsg;
			usr->inMsg = 0;
			
			// rewind circular buffer if there's no more data in it.
			if (usr->input.isEmpty())
				usr->input.rewind();
		}
		else // quite dead
			break;
	}
}

message_ref Server::msgUserEvent(const User* usr, const Session* session, const uint8_t event) const throw(std::bad_alloc)
{
	assert(usr != 0);
	assert(session != 0);
	
	#if defined(DEBUG_SERVER) and !defined(NDEBUG)
	std::cout << "Server::msgUserEvent(user: "
		<< static_cast<int>(usr->id) << ")" << std::endl;
	#endif
	
	protocol::UserInfo *uevent = new protocol::UserInfo(
		session->mode,
		event,
		usr->nlen,
		(usr->nlen == 0 ? 0 : new char[usr->nlen])
	);
	
	uevent->user_id = usr->id;
	uevent->session_id = session->id;
	
	memcpy(uevent->name, usr->name, usr->nlen);
	
	return message_ref(uevent);
}

inline
bool Server::uInSession(User* usr, const uint8_t session) const throw()
{
	assert(usr != 0);
	return usr->sessions.find(session) != usr->sessions.end();
}

bool Server::sessionExists(uint8_t session) const throw()
{
	assert(session != protocol::Global);
	
	if (sessions.find(session) == sessions.end())
		return false;
	
	return true;
}

void Server::uHandleMsg(User*& usr) throw(std::bad_alloc)
{
	assert(usr != 0);
	assert(usr->inMsg != 0);
	
	#if defined(DEBUG_SERVER) and !defined(NDEBUG)
	std::cout << "Server::uHandleMsg(user: " << static_cast<int>(usr->id)
		<< ", type: " << static_cast<int>(usr->inMsg->type) << ")" << std::endl;
	#endif
	
	assert(usr->state == uState::active);
	
	// TODO
	switch (usr->inMsg->type)
	{
	case protocol::type::ToolInfo:
		// check for protocol violations
		#ifdef CHECK_VIOLATIONS
		if (!fIsSet(usr->tags, uTag::CanChange))
		{
			std::cerr << "Protocol violation from user #"
				<< static_cast<int>(usr->id) << std::endl
				<< "Reason: Unexpected tool info" << std::endl;
			uRemove(usr, protocol::user_event::Violation);
			break;
		}
		fSet(usr->tags, uTag::HaveTool);
		#endif // CHECK_VIOLATIONS
	case protocol::type::StrokeInfo:
		// check for protocol violations
		#ifdef CHECK_VIOLATIONS
		if (usr->inMsg->type == protocol::type::StrokeInfo)
		{
			/*
			if (!fIsSet(usr->tags, uTag::HaveTool))
			{
				std::cerr << "Protocol violation from user: "
					<< static_cast<int>(usr->id) << std::endl
					<< "Reason: Stroke info without tool." << std::endl;
				uRemove(usr, protocol::user_event::Violation);
				break;
			}
			*/
			fClr(usr->tags, uTag::CanChange);
		}
		#endif // CHECK_VIOLATIONS
	case protocol::type::StrokeEnd:
		// check for protocol violations
		#ifdef CHECK_VIOLATIONS
		if (usr->inMsg->type == protocol::type::StrokeEnd)
		{
			if (!fIsSet(usr->tags, uTag::HaveTool))
			{
				std::cerr << "Protocol violation from user #"
					<< static_cast<int>(usr->id) << std::endl
					<< "Reason: Stroke info without tool." << std::endl;
				uRemove(usr, protocol::user_event::Violation);
				break;
			}
			fSet(usr->tags, uTag::CanChange);
		}
		#endif // CHECK_VIOLATIONS
		
		if (usr->inMsg->type == protocol::type::StrokeInfo)
			++usr->strokes;
		else if (usr->inMsg->type == protocol::type::StrokeEnd)
			usr->strokes = 0;
		
		// no session selected
		if (usr->session == 0)
		{
			#ifndef NDEBUG
			if (usr->strokes == 1)
				std::cerr << "User #" << static_cast<int>(usr->id)
					<< " attempts to draw on null session." << std::endl;
			#endif
			break;
		}
		
		#ifdef LAYER_SUPPORT
		if (usr->inMsg->type != protocol::type::ToolInfo
			and usr->layer == protocol::null_layer)
		{
			#ifndef NDEBUG
			if (usr->strokes == 1)
			{
				std::cout << "User #" << static_cast<int>(usr->id)
					<< " is drawing on null layer!" << std::endl;
			}
			#endif
			
			break;
		}
		#endif
		
		#ifdef CHECK_VIOLATIONS
		if ((usr->inMsg->user_id != protocol::null_user)
			#ifdef LENIENT
			or (usr->inMsg->user_id != usr->id)
			#endif // LENIENT
		)
		{
			std::cerr << "Client attempted to impersonate someone." << std::endl;
			uRemove(usr, protocol::user_event::Violation);
			break;
		}
		#endif // CHECK_VIOLATIONS
		
		// user or session is locked
		if (usr->session->locked or usr->a_locked)
		{
			// TODO: Warn user?
			#ifndef NDEBUG
			if (usr->strokes == 1)
			{
				std::cerr << "User #" << static_cast<int>(usr->id)
					<< " tries to draw, despite the lock." << std::endl;
				
				if (usr->session->locked)
					std::cerr << "Clarification: Sesssion #"
						<< static_cast<int>(usr->session->id) << " is locked." << std::endl;
				else
					std::cerr << "Clarification: User is locked in session #"
						<< static_cast<int>(usr->session->id) << "." << std::endl;
			}
			#endif
			break;
		}
		
		// make sure the user id is correct
		usr->inMsg->user_id = usr->id;
		
		/*
		{
			// scroll to the last messag in linked-list
			//message_ref ref;
			protocol::Message* msg = usr->inMsg;
			while (msg->next != 0)
				msg = msg->next;
		}
		*/
		
		Propagate(
			usr->session,
			message_ref(usr->inMsg),
			(usr->c_acks ? usr : 0)
		);
		
		usr->inMsg = 0;
		break;
	case protocol::type::Acknowledgement:
		uHandleAck(usr);
		break;
	case protocol::type::SessionSelect:
		#ifdef CHECK_VIOLATIONS
		if (!fIsSet(usr->tags, uTag::CanChange))
		{
			std::cerr << "Protocol violation from user." << usr->id << std::endl
				<< "Session change in middle of something." << std::endl;
			
			uRemove(usr, protocol::user_event::Violation);
			break;
		}
		else
		#endif
		{
			// check other sessions
			const usr_session_iterator usi(usr->sessions.find(usr->inMsg->session_id));
			if (usi == usr->sessions.end())
			{
				// user not in the selected session, null selected session
				uSendMsg(usr, msgError(usr->inMsg->session_id, protocol::error::NotSubscribed));
				usr->session = 0;
				break;
			}
			
			usr->inMsg->user_id = usr->id;
			usr->session = usi->second->session;
			
			usr->a_locked = usi->second->locked;
			usr->a_deaf = usi->second->deaf;
			usr->a_muted = usi->second->muted;
			
			Propagate(
				usr->session,
				message_ref(usr->inMsg),
				(usr->c_acks ? usr : 0)
			);
			usr->inMsg = 0;
			
			#ifdef CHECK_VIOLATIONS
			fSet(usr->tags, uTag::CanChange);
			#endif
		}
		break;
	case protocol::type::UserInfo:
		// TODO?
		break;
	case protocol::type::Raster:
		uTunnelRaster(usr);
		break;
	case protocol::type::Deflate:
		// Deflate extension
		#if !defined(HAVE_ZLIB)
		uRemove(usr, protocol::user_event::Dropped);
		#else
		DeflateReprocess(usr, usr->inMsg);
		#endif
		break;
	case protocol::type::Chat:
	case protocol::type::Palette:
		{
			message_ref pmsg(usr->inMsg);
			pmsg->user_id = usr->id;
			
			const usr_session_iterator usi(usr->sessions.find(usr->inMsg->session_id));
			if (usi == usr->sessions.end())
			{
				uSendMsg(usr,
					msgError(usr->inMsg->session_id, protocol::error::NotSubscribed)
				);
				
				break;
			}
			
			Propagate(
				usi->second->session,
				pmsg,
				(usr->c_acks ? usr : 0)
			);
			usr->inMsg = 0;
		}
		break;
	case protocol::type::Unsubscribe:
		#ifdef CHECK_VIOLATIONS
		if (!fIsSet(usr->tags, uTag::CanChange))
		{
			std::cerr << "Protocol violation from user #"
				<< static_cast<int>(usr->id) << std::endl
				<< "Reason: Unsubscribe in middle of something." << std::endl;
			uRemove(usr, protocol::user_event::Violation);
			break;
		}
		#endif
		{
			const sessionmap_iterator si(sessions.find(usr->inMsg->session_id));
			
			if (si == sessions.end())
			{
				#ifndef NDEBUG
				std::cerr << "(unsubscribe) no such session #"
					<< static_cast<int>(usr->inMsg->session_id) << std::endl;
				#endif
				
				uSendMsg(usr, msgError(usr->inMsg->session_id, protocol::error::UnknownSession));
			}
			else
			{
				uSendMsg(usr, msgAck(usr->inMsg->session_id, usr->inMsg->type));
				
				uLeaveSession(usr, si->second);
			}
		}
		break;
	case protocol::type::Subscribe:
		#ifdef CHECK_VIOLATIONS
		if (!fIsSet(usr->tags, uTag::CanChange))
		{
			std::cerr << "Protocol violation from user #"
				<< static_cast<int>(usr->id) << std::endl
				<< "Reason: Subscribe in middle of something." << std::endl;
			uRemove(usr, protocol::user_event::Violation);
			break;
		}
		#endif
		if (usr->syncing == protocol::Global)
		{
			const sessionmap_iterator si(sessions.find(usr->inMsg->session_id));
			if (si == sessions.end())
			{
				// session not found
				#ifndef NDEBUG
				std::cerr << "(subscribe) no such session #"
					<< static_cast<int>(usr->inMsg->session_id) << std::endl;
				#endif
				
				uSendMsg(usr, msgError(usr->inMsg->session_id, protocol::error::UnknownSession));
				break;
			}
			Session *session = si->second;
			
			if (!uInSession(usr, usr->inMsg->session_id))
			{
				// Test userlimit
				if (session->canJoin() == false)
				{
					uSendMsg(usr, msgError(usr->inMsg->session_id, protocol::error::SessionFull));
					break;
				}
				
				if (session->level != usr->level)
				{
					uSendMsg(usr, msgError(protocol::Global, protocol::error::ImplementationMismatch));
					break;
				}
				
				if (session->password != 0)
				{
					uSendMsg(usr, msgAuth(usr, session->id));
					break;
				}
				
				// join session
				uSendMsg(usr, msgAck(usr->inMsg->session_id, usr->inMsg->type));
				uJoinSession(usr, session);
			}
			else
			{
				// already subscribed
				uSendMsg(usr, msgError(
					usr->inMsg->session_id, protocol::error::InvalidRequest)
				);
			}
		}
		else
		{
			// already syncing some session, so we don't bother handling this request.
			uSendMsg(usr, msgError(usr->inMsg->session_id, protocol::error::SyncInProgress));
		}
		break;
	case protocol::type::ListSessions:
		if (sessions.size() != 0)
		{
			sessionmap_iterator si(sessions.begin());
			for (; si != sessions.end(); ++si)
			{
				uSendMsg(usr, msgSessionInfo(si->second));
			}
		}
		
		uSendMsg(usr, msgAck(protocol::Global, usr->inMsg->type));
		break;
	case protocol::type::SessionEvent:
		{
			const sessionmap_iterator si(sessions.find(usr->inMsg->session_id));
			if (si == sessions.end())
			{
				uSendMsg(usr, msgError(usr->inMsg->session_id, protocol::error::UnknownSession));
				return;
			}
			
			usr->inMsg->user_id = usr->id;
			Session *session = si->second;
			uSessionEvent(session, usr);
			//usr->inMsg = 0;
		}
		break;
	case protocol::type::LayerEvent:
		// TODO
		break;
	case protocol::type::LayerSelect:
		{
			protocol::LayerSelect *layer = static_cast<protocol::LayerSelect*>(usr->inMsg);
			
			const usr_session_iterator ui(usr->sessions.find(layer->session_id));
			if (ui == usr->sessions.end())
			{
				// layer select for non-subscribed session
				uSendMsg(usr, msgError(layer->session_id, protocol::error::NotSubscribed));
				break;
			}
			else if (layer->layer_id == ui->second->layer)
			{
				// tried to select currently selected layer
				uSendMsg(usr, msgError(layer->session_id, protocol::error::InvalidLayer));
				break;
			}
			else if (ui->second->layer_lock != protocol::null_layer
				and ui->second->layer_lock != layer->layer_id)
			{
				// if user is locked to another layer
				uSendMsg(usr, msgError(layer->session_id, protocol::error::InvalidLayer));
				break;
			}
			
			// useless temporary
			Session *session = ui->second->session;
			
			// Find layer and check if its locked
			const session_layer_iterator li(session->layers.find(layer->layer_id));
			if (li == session->layers.end())
			{
				uSendMsg(usr, msgError(layer->session_id, protocol::error::UnknownLayer));
				break;
			}
			else if (li->second.locked)
			{
				uSendMsg(usr, msgError(layer->session_id, protocol::error::LayerLocked));
				break;
			}
			
			uSendMsg(usr, msgAck(layer->session_id, layer->type));
			
			layer->user_id = usr->id;
			
			Propagate(
				session,
				message_ref(layer),
				(usr->c_acks ? usr : 0)
			);
			
			ui->second->layer = layer->layer_id;
		}
		break;
	case protocol::type::Instruction:
		uHandleInstruction(usr);
		break;
	case protocol::type::Password:
		{
			sessionmap_iterator si;
			protocol::Password *msg = static_cast<protocol::Password*>(usr->inMsg);
			if (msg->session_id == protocol::Global)
			{
				// Admin login
				if (a_password == 0)
				{
					std::cerr << "User tries to pass password even though we've disallowed it." << std::endl;
					uSendMsg(usr, msgError(msg->session_id, protocol::error::PasswordFailure));
					return;
				}
				
				hash.Update(reinterpret_cast<uint8_t*>(a_password), a_pw_len);
			}
			else
			{
				if (usr->syncing != protocol::Global)
				{
					// already syncing some session, so we don't bother handling this request.
					uSendMsg(usr, msgError(usr->inMsg->session_id, protocol::error::SyncInProgress));
					break;
				}
				
				si = sessions.find(msg->session_id);
				if (si == sessions.end())
				{
					// session doesn't exist
					uSendMsg(usr, msgError(msg->session_id, protocol::error::UnknownSession));
					break;
				}
				
				if (uInSession(usr, msg->session_id))
				{
					// already in session
					uSendMsg(usr, msgError(msg->session_id, protocol::error::InvalidRequest));
					break;
				}
				
				// Test userlimit
				if (si->second->canJoin() == false)
				{
					uSendMsg(usr, msgError(usr->inMsg->session_id, protocol::error::SessionFull));
					break;
				}
				
				hash.Update(reinterpret_cast<uint8_t*>(si->second->password), si->second->pw_len);
			}
			
			hash.Update(reinterpret_cast<uint8_t*>(usr->seed), 4);
			hash.Final();
			char digest[protocol::password_hash_size];
			hash.GetHash(reinterpret_cast<uint8_t*>(digest));
			hash.Reset();
			
			uRegenSeed(usr);
			
			if (memcmp(digest, msg->data, protocol::password_hash_size) != 0)
			{
				// mismatch, send error or disconnect.
				uSendMsg(usr, msgError(msg->session_id, protocol::error::PasswordFailure));
				return;
			}
			
			uSendMsg(usr, msgAck(msg->session_id, msg->type));
			
			if (msg->session_id == protocol::Global)
			{
				// set as admin
				usr->isAdmin = true;
			}
			else
			{
				// join session
				// uSendMsg(usr, msgAck(usr->inMsg->session_id, usr->inMsg->type));
				Session* session = si->second;
				uJoinSession(usr, session);
			}
		}
		break;
	default:
		std::cerr << "Unknown message: #" << static_cast<int>(usr->inMsg->type)
			<< ", from user: #" << static_cast<int>(usr->id)
			<< " (dropping)" << std::endl;
		uRemove(usr, protocol::user_event::Dropped);
		break;
	}
}

#if defined(HAVE_ZLIB)
void Server::DeflateReprocess(User*& usr, protocol::Message* msg) throw(std::bad_alloc)
{
	assert(usr != 0);
	assert(msg != 0);
	
	#if !defined(NDEBUG)
	std::cout << "Server::DeflateReprocess(user: " << static_cast<int>(usr->id) << ")" << std::endl;
	#endif
	
	protocol::Deflate* stream = static_cast<protocol::Deflate*>(usr->inMsg);
	usr->inMsg = 0;
	
	#if !defined(NDEBUG)
	std::cout << "Compressed: " << stream->length
		<< " bytes, uncompressed: " << stream->uncompressed << " bytes." << std::endl;
	#endif
	
	char* outbuf = new char[stream->uncompressed];
	unsigned long outlen = stream->uncompressed;
	const int r = uncompress(reinterpret_cast<unsigned char*>(outbuf), &outlen, reinterpret_cast<unsigned char*>(stream->data), stream->length);
	
	switch (r)
	{
	default:
	case Z_OK:
		{
			// Store input buffer. (... why?)
			Buffer temp;
			temp << usr->input;
			
			// Set the uncompressed data stream as the input buffer.
			usr->input.setBuffer(outbuf, stream->uncompressed);
			
			// Process the data.
			uProcessData(usr);
			
			// Since uProcessData might've deleted the user
			if (usr != 0) break;
			
			// Restore input buffer
			usr->input << temp;
		}
		break;
	case Z_MEM_ERROR:
		// TODO: Out of Memory
		throw std::bad_alloc();
		//break;
	case Z_BUF_ERROR:
	case Z_DATA_ERROR:
		// Input buffer corrupted
		std::cout << "Corrupted data from user #" << static_cast<int>(usr->id)
			<< ", dropping." << std::endl;
		uRemove(usr, protocol::user_event::Dropped);
		delete [] outbuf;
		break;
	}
}
#endif

void Server::uHandleAck(User*& usr) throw()
{
	assert(usr != 0);
	assert(usr->inMsg != 0);
	
	const protocol::Acknowledgement *ack = static_cast<protocol::Acknowledgement*>(usr->inMsg);
	
	switch (ack->event)
	{
	case protocol::type::SyncWait:
		{
			// check active session first
			const usr_session_iterator us(usr->sessions.find(ack->session_id));
			if (us == usr->sessions.end())
			{
				uSendMsg(usr, msgError(ack->session_id, protocol::error::NotSubscribed));
				return;
			}
			else if (us->second->syncWait)
			{
				#ifndef NDEBUG
				std::cout << "Another ACK/SyncWait for same session. Kickin'!" << std::endl;
				#endif
				
				uRemove(usr, protocol::user_event::Dropped);
				return;
			}
			else
			{
				us->second->syncWait = true;
			}
			
			Session *session = us->second->session;
			--session->syncCounter;
			
			if (session->syncCounter == 0)
			{
				#ifndef NDEBUG
				std::cout << "Synchronizing raster for session #"
					<< static_cast<int>(session->id) << "." << std::endl;
				#endif
				
				SyncSession(session);
			}
			else
			{
				#ifndef NDEBUG
				std::cout << "ACK/SyncWait counter: " << session->syncCounter << std::endl;
				#endif
			}
		}
		break;
	default:
		#ifndef NDEBUG
		std::cout << "ACK/SomethingWeDoNotCareAboutButShouldValidateNoneTheLess" << std::endl;
		#endif
		// don't care..
		break;
	}
}

void Server::uTunnelRaster(User* usr) throw()
{
	assert(usr != 0);
	assert(usr->inMsg != 0);
	
	#if defined(DEBUG_SERVER) and !defined(NDEBUG)
	std::cout << "Server::uTunnelRaster(from: "
		<< static_cast<int>(usr->id) << ")" << std::endl;
	#endif
	
	protocol::Raster *raster = static_cast<protocol::Raster*>(usr->inMsg);
	
	const bool last = (raster->offset + raster->length == raster->size);
	
	if (!uInSession(usr, raster->session_id))
	{
		std::cerr << "Raster for unsubscribed session #"
			<< static_cast<int>(raster->session_id)
			<< ", from user #" << static_cast<int>(usr->id)
			<< std::endl;
		
		if (!last)
		{
			message_ref cancel_ref(new protocol::Cancel);
			cancel_ref->session_id = raster->session_id;
			uSendMsg(usr, cancel_ref);
		}
		
		return;
	}
	
	// get users
	const std::pair<tunnelmap_iterator,tunnelmap_iterator> ft(tunnel.equal_range(usr->sock->fd()));
	if (ft.first == ft.second)
	{
		std::cerr << "Un-tunneled raster from user #"
			<< static_cast<int>(usr->id)
			<< ", for session #" << static_cast<int>(raster->session_id)
			<< std::endl;
		
		// We assume the raster was for user/s who disappeared
		// before we could cancel the request.
		
		if (!last)
		{
			message_ref cancel_ref(new protocol::Cancel);
			cancel_ref->session_id = raster->session_id;
			uSendMsg(usr, cancel_ref);
		}
		return;
	}
	
	// ACK raster
	uSendMsg(usr, msgAck(usr->inMsg->session_id, usr->inMsg->type));
	
	// Forward to users.
	tunnelmap_iterator ti(ft.first);
	User *usr_ptr;
	
	message_ref raster_ref(raster);
	
	for (; ti != ft.second; ++ti)
	{
		usr_ptr = users[ti->second];
		uSendMsg(usr_ptr, raster_ref);
		if (last) usr_ptr->syncing = false;
	}
	
	// Break tunnel/s if that was the last raster piece.
	if (last) tunnel.erase(usr->sock->fd());
	
	// Avoid premature deletion of raster data.
	usr->inMsg = 0;
}

void Server::uSessionEvent(Session*& session, User*& usr) throw()
{
	assert(session != 0);
	assert(usr != 0);
	assert(usr->inMsg->type == protocol::type::SessionEvent);
	
	#if defined(DEBUG_SERVER) and !defined(NDEBUG)
	std::cout << "Server::uSessionEvent(session: "
		<< static_cast<int>(session->id) << ")" << std::endl;
	#endif
	
	if (!usr->isAdmin
		and (session->owner != usr->id))
	{
		uSendMsg(usr, msgError(session->id, protocol::error::Unauthorized));
		return;
	}
	
	protocol::SessionEvent *event = static_cast<protocol::SessionEvent*>(usr->inMsg);
	
	switch (event->action)
	{
	case protocol::session_event::Kick:
		{
			const session_usr_iterator sui(session->users.find(event->target));
			if (sui == session->users.end())
			{
				// user not found in session
				uSendMsg(usr, msgError(session->id, protocol::error::UnknownUser));
				break;
			}
			
			Propagate(session, message_ref(event));
			usr->inMsg = 0;
			User *usr_ptr = sui->second;
			uLeaveSession(usr_ptr, session, protocol::user_event::Kicked);
		}
		break;
	case protocol::session_event::Lock:
	case protocol::session_event::Unlock:
		if (event->target == protocol::null_user)
		{
			// Lock whole board
			
			#ifndef NDEBUG
			std::cout << "Changing lock state for session #"
				<< static_cast<int>(session->id)
				<< " to "
				<< (event->action == protocol::session_event::Lock ? "enabled" : "disabled") << std::endl;
			#endif
			
			session->locked = (event->action == protocol::session_event::Lock ? true : false);
		}
		else
		{
			// Lock single user
			
			#ifndef NDEBUG
			std::cout << "Changing lock state for user #"
				<< static_cast<int>(event->target)
				<< " to "
				<< (event->action == protocol::session_event::Lock ? "enabled" : "disabled")
				<< " in session #" << static_cast<int>(session->id) << std::endl;
			#endif
			
			// Find user
			const session_usr_iterator session_usr(session->users.find(event->target));
			if (session_usr == session->users.end())
			{
				uSendMsg(usr, msgError(session->id, protocol::error::UnknownUser));
				break;
			}
			User *usr = session_usr->second;
			
			// Find user's session instance (SessionData*)
			const usr_session_iterator usr_session(usr->sessions.find(session->id));
			if (usr_session == usr->sessions.end())
			{
				uSendMsg(usr, msgError(session->id, protocol::error::NotInSession));
				break;
			}
			
			if (event->aux == protocol::null_layer)
			{
				usr_session->second->locked = (event->action == protocol::session_event::Lock);
				
				// Copy active session
				if (usr->session->id == event->session_id)
					usr->a_locked = usr_session->second->locked;
			}
			else
			{
				if (event->action == protocol::session_event::Lock)
				{
					#ifndef NDEBUG
					std::cout << "Lock ignored, limiting user to layer #"
						<< static_cast<int>(event->aux) << std::endl;
					#endif
					
					usr_session->second->layer_lock = event->aux;
					
					// Null-ize the active layer if the target layer is not the active one.
					if (usr_session->second->layer != usr_session->second->layer_lock)
						usr_session->second->layer = protocol::null_layer;
					if (usr->session->id == event->session_id)
						usr->layer = protocol::null_layer;
				}
				else // unlock
				{
					#ifndef NDEBUG
					std::cout << "Lock ignored, releasing user from layer #"
						<< static_cast<int>(event->aux) << std::endl;
					#endif
					
					usr_session->second->layer_lock = protocol::null_layer;
				}
			}
		}
		
		Propagate(session, message_ref(event));
		usr->inMsg = 0;
		
		break;
	case protocol::session_event::Delegate:
		{
			const session_usr_iterator sui(session->users.find(event->target));
			if (sui == session->users.end())
			{
				// User not found
				uSendMsg(usr, msgError(session->id, protocol::error::NotInSession));
				break;
			}
			
			session->owner = sui->second->id;
			
			Propagate(
				session,
				message_ref(event),
				(usr->c_acks ? usr : 0)
			);
			
			usr->inMsg = 0;
		}
		break;
	case protocol::session_event::Mute:
	case protocol::session_event::Unmute:
		{
			const session_usr_iterator sui(session->users.find(event->target));
			if (sui == session->users.end())
			{
				// user not found
				uSendMsg(usr, msgError(session->id, protocol::error::NotInSession));
				break;
			}
			
			usr_session_iterator usi(sui->second->sessions.find(session->id));
			if (usi == sui->second->sessions.end())
			{
				// user not in session
				uSendMsg(usr, msgError(session->id, protocol::error::NotInSession));
				break;
			}
			
			// Set mode
			usi->second->muted = (event->action == protocol::session_event::Mute);
			
			// Copy to active session's mode, too.
			if (usr->session->id == event->target)
				usr->a_muted = usi->second->muted;
			
			Propagate(
				session,
				message_ref(event),
				(usr->c_acks ? usr : 0)
			);
			
			usr->inMsg = 0;
		}
		break;
	case protocol::session_event::Persist:
		// TODO
		break;
	case protocol::session_event::CacheRaster:
		// TODO
		break;
	default:
		std::cerr << "Unknown session action: "
			<< static_cast<int>(event->action) << ")" << std::endl;
		uRemove(usr, protocol::user_event::Violation);
		return;
	}
}

void Server::uHandleInstruction(User*& usr) throw(std::bad_alloc)
{
	assert(usr != 0);
	
	#if defined(DEBUG_SERVER) and !defined(NDEBUG)
	std::cout << "Server::uHandleInstruction(user: "
		<< static_cast<int>(usr->id) << ")" << std::endl;
	#endif
	
	assert(usr->inMsg != 0);
	assert(usr->inMsg->type == protocol::type::Instruction);
	
	protocol::Instruction *msg = static_cast<protocol::Instruction*>(usr->inMsg);
	
	switch (msg->command)
	{
	case protocol::admin::command::Create:
		if (!usr->isAdmin) { break; }
		// limited scope for switch/case
		{
			const uint8_t session_id = getSessionID();
			
			if (session_id == protocol::Global)
			{
				uSendMsg(usr, msgError(msg->session_id, protocol::error::SessionLimit));
				return;
			}
			
			Session *session(new Session);
			session->id = session_id;
			session->level = usr->level; // inherit user's feature level
			session->limit = msg->aux_data; // user limit
			session->mode = msg->aux_data2; // user mode
			
			if (session->limit < 2)
			{
				#ifndef NDEBUG
				std::cerr << "Attempted to create single user session." << std::endl;
				#endif
				
				uSendMsg(usr, msgError(msg->session_id, protocol::error::InvalidData));
				delete session;
				return;
			}
			
			const size_t crop = sizeof(session->width) + sizeof(session->height);
			if (msg->length < crop)
			{
				#ifndef NDEBUG
				std::cerr << "Less data than required" << std::endl;
				#endif
				
				uRemove(usr, protocol::user_event::Violation);
				delete session;
				return;
			}
			
			memcpy_t(session->width, msg->data);
			memcpy_t(session->height, msg->data+sizeof(session->width));
			
			bswap(session->width);
			bswap(session->height);
			
			if (session->width < min_dimension or session->height < min_dimension)
			{
				uSendMsg(usr, msgError(msg->session_id, protocol::error::TooSmall));
				delete session;
				return;
			}
			
			if (msg->length < crop)
			{
				std::cerr << "Invalid data size in instruction: 'Create'." << std::endl;
				uSendMsg(usr, msgError(protocol::Global, protocol::error::InvalidData));
				delete session;
				return;
			}
			else if (msg->length != crop)
			{
				session->len = (msg->length - crop);
				session->title = new char[session->len];
				memcpy(session->title, msg->data+crop, session->len);
			}
			else
			{
				session->len = 0;
				#ifndef NDEBUG
				std::cout << "No title set for session." << std::endl;
				#endif
			}
			
			if (fIsSet(requirements, protocol::requirements::EnforceUnique)
				and !validateSessionTitle(session))
			{
				#ifndef NDEBUG
				std::cerr << "Session title not unique." << std::endl;
				#endif
				
				uSendMsg(usr, msgError(msg->session_id, protocol::error::NotUnique));
				delete session;
				return;
			}
			
			session->owner = usr->id;
			
			sessions[session->id] = session;
			
			std::cout << "Session #" << static_cast<int>(session->id) << " created!" << std::endl
				<< "Dimensions: " << session->width << " x " << session->height << std::endl
				<< "User limit: " << static_cast<int>(session->limit)
				<< ", default mode: " << static_cast<int>(session->mode)
				<< ", level: " << static_cast<int>(session->level) << std::endl
				<< "Owner: " << static_cast<int>(usr->id)
				<< ", from: " << usr->sock->address() << std::endl;
			
			uSendMsg(usr, msgAck(msg->session_id, msg->type));
		}
		return;
	case protocol::admin::command::Destroy:
		{
			const sessionmap_iterator si(sessions.find(msg->session_id));
			if (si == sessions.end())
			{
				uSendMsg(usr, msgError(msg->session_id, protocol::error::UnknownSession));
				return;
			}
			Session *session = si->second;
			
			// Check session ownership
			if (!usr->isAdmin
				and (session->owner != usr->id))
			{
				uSendMsg(usr, msgError(session->id, protocol::error::Unauthorized));
				break;
			}
			
			// tell users session was lost
			message_ref err = msgError(session->id, protocol::error::SessionLost);
			Propagate(session, err, 0, true);
			
			// clean session users
			session_usr_iterator sui(session->users.begin());
			User* usr_ptr;
			for (; sui != session->users.end(); ++sui)
			{
				usr_ptr = sui->second;
				uLeaveSession(usr_ptr, session, protocol::user_event::None);
			}
			
			// destruct
			delete session;
			sessions.erase(si->first);
		}
		break;
	case protocol::admin::command::Alter:
		{
			const sessionmap_iterator si(sessions.find(msg->session_id));
			if (si == sessions.end())
			{
				uSendMsg(usr, msgError(msg->session_id, protocol::error::UnknownSession));
				return;
			}
			Session *session = si->second;
			
			// Check session ownership
			if (!usr->isAdmin
				and (session->owner != usr->id))
			{
				uSendMsg(usr, msgError(session->id, protocol::error::Unauthorized));
				break;
			}
			
			if (msg->length < sizeof(session->width) + sizeof(session->height))
			{
				std::cerr << "Protocol violation from user #"
					<< static_cast<int>(usr->id) << std::endl
					<< "Reason: Too little data for Alter instruction." << std::endl;
				uRemove(usr, protocol::user_event::Violation);
				return;
			}
			
			// user limit adjustment
			session->limit = msg->aux_data;
			
			// default user mode adjustment
			session->mode = msg->aux_data2;
			
			// session canvas's size
			uint16_t width, height;
			
			memcpy_t(width, msg->data),
			memcpy_t(height, msg->data+sizeof(width));
			
			bswap(width), bswap(height);
			
			if ((width < session->width) or (height < session->height))
			{
				std::cerr << "Protocol violation from user #"
					<< static_cast<int>(usr->id) << std::endl
					<< "Reason: Attempted to reduce session's canvas size." << std::endl;
				uRemove(usr, protocol::user_event::Violation);
				return;
			}
			
			session->height = height,
			session->width = width;
			
			// new title
			if (msg->length > (sizeof(width) + sizeof(height)))
			{
				session->len = msg->length - (sizeof(width) + sizeof(height));
				delete [] session->title;
				session->title = new char[session->len];
				memcpy(session->title, msg->data+sizeof(width)+sizeof(height), session->len);
			}
			
			#ifndef NDEBUG
			std::cout << "Session #" << static_cast<int>(session->id) << " altered!" << std::endl
				<< "Dimensions: " << width << " x " << height << std::endl
				<< "User limit: " << static_cast<int>(session->limit) << ", default mode: "
				<< static_cast<int>(session->mode) << std::endl;
			#endif // NDEBUG
			
			Propagate(session, msgSessionInfo(session));
		}
		break;
	case protocol::admin::command::Password:
		if (msg->session_id == protocol::Global)
		{
			if (!usr->isAdmin) { break; }
			
			if (password != 0)
				delete [] password;
			
			password = msg->data;
			pw_len = msg->length;
			
			msg->data = 0;
			//msg->length = 0;
			
			#ifndef NDEBUG
			std::cout << "Server password changed." << std::endl;
			#endif
			
			uSendMsg(usr, msgAck(msg->session_id, msg->type));
		}
		else
		{
			const sessionmap_iterator si(sessions.find(msg->session_id));
			if (si == sessions.end())
			{
				uSendMsg(usr, msgError(msg->session_id, protocol::error::UnknownSession));
				return;
			}
			Session *session = si->second;
			
			// Check session ownership
			if (!usr->isAdmin
				and (session->owner != usr->id))
			{
				uSendMsg(usr, msgError(session->id, protocol::error::Unauthorized));
				break;
			}
			
			#ifndef NDEBUG
			std::cout << "Password set for session #"
				<< static_cast<int>(msg->session_id) << std::endl;
			#endif
			
			if (session->password != 0)
				delete [] session->password;
			
			session->password = msg->data;
			session->pw_len = msg->length;
			msg->data = 0;
			
			uSendMsg(usr, msgAck(msg->session_id, msg->type));
			return;
		}
		break;
	case protocol::admin::command::Authenticate:
		#ifndef NDEBUG
		std::cout << "User #" << static_cast<int>(usr->id) << " wishes to authenticate itself as an admin." << std::endl;
		#endif
		if (a_password == 0)
		{
			// no admin password set
			uSendMsg(usr, msgError(msg->session_id, protocol::error::Unauthorized));
		}
		else
		{
			// request password
			uSendMsg(usr, msgAuth(usr, protocol::Global));
		}
		return;
	case protocol::admin::command::Shutdown:
		if (!usr->isAdmin) { break; }
		// Shutdown server..
		state = server::state::Exiting;
		break;
	default:
		#ifndef NDEBUG
		std::cerr << "Unrecognized command: "
			<< static_cast<int>(msg->command) << std::endl;
		#endif
		uRemove(usr, protocol::user_event::Dropped);
		return;
	}
	
	// Allow session owners to alter sessions, but warn others.
	if (!usr->isAdmin)
		uSendMsg(usr, msgError(msg->session_id, protocol::error::Unauthorized));
}

void Server::uHandleLogin(User*& usr) throw(std::bad_alloc)
{
	assert(usr != 0);
	assert(usr->inMsg != 0);
	
	#if defined(DEBUG_SERVER) and !defined(NDEBUG)
	std::cout << "Server::uHandleLogin(user: " << static_cast<int>(usr->id)
		<< ", type: " << static_cast<int>(usr->inMsg->type) << ")" << std::endl;
	#endif
	
	switch (usr->state)
	{
	case uState::login:
		if (usr->inMsg->type == protocol::type::UserInfo)
		{
			protocol::UserInfo *msg = static_cast<protocol::UserInfo*>(usr->inMsg);
			
			#ifdef CHECK_VIOLATIONS
			if ( (msg->user_id != protocol::null_user)
				or (msg->session_id != protocol::Global)
				or (msg->event != protocol::user_event::Login))
			{
				std::cerr << "Protocol violation from user #"
					<< static_cast<int>(usr->id) << std::endl
					<< "Reason: ";
				
				if (msg->user_id != protocol::null_user)
				{
					std::cerr << "Client assumed it knows its user ID." << std::endl;
				}
				else if (msg->session_id != protocol::Global)
				{
					std::cerr << "Incorrect login session identifier." << std::endl;
				}
				else if (msg->event != protocol::user_event::Login)
				{
					std::cerr << "Wrong user event for login." << std::endl;
				}
				
				uRemove(usr, protocol::user_event::Violation);
				break;
			}
			#endif // CHECK_VIOLATIONS
			
			if (msg->length > name_len_limit)
			{
				#ifndef NDEBUG
				std::cerr << "Name too long: " << static_cast<int>(msg->length)
					<< " > " << static_cast<int>(name_len_limit) << std::endl;
				#endif
				
				uSendMsg(usr, msgError(msg->session_id, protocol::error::TooLong));
				
				//uRemove(usr, protocol::user_event::Dropped);
				break;
			}
			
			// assign user their own name
			usr->name = msg->name;
			usr->nlen = msg->length;
			
			if (fIsSet(requirements, protocol::requirements::EnforceUnique)
				and !validateUserName(usr))
			{
				#ifndef NDEBUG
				std::cerr << "User name not unique." << std::endl;
				#endif
				
				uSendMsg(usr, msgError(msg->session_id, protocol::error::NotUnique));
				
				//uRemove(usr, protocol::user_event::Dropped);
				break;
			}
			
			// assign message the user's id (for sending back)
			msg->user_id = usr->id;
			
			// null the message's name information, so they don't get deleted
			msg->length = 0;
			msg->name = 0;
			
			usr->setAMode(default_user_mode);
			
			const std::string IPPort(usr->sock->address());
			const std::string::size_type ns(IPPort.find_last_of(":", IPPort.length()-1));
			assert(ns != std::string::npos);
			const std::string IP(IPPort.substr(0, ns));
			
			if (LocalhostAdmin
				 // Loopback address
				#ifdef IPV6_SUPPORT
				and (IP == std::string("::1")))
				#else
				and (IP == std::string("127.0.0.1")))
				#endif // IPV6_SUPPORT
			{
				// auto admin promotion.
				// also, don't put any other flags on the user.
				usr->isAdmin = true;
			}
			
			msg->mode = usr->getAMode();
			
			// reply
			uSendMsg(usr, message_ref(msg));
			
			usr->state = uState::active;
			
			// remove fake timer
			utimer.erase(utimer.find(usr));
			
			usr->deadtime = 0;
			
			usr->inMsg = 0;
		}
		else
		{
			// wrong message type
			uRemove(usr, protocol::user_event::Violation);
		}
		break;
	case uState::login_auth:
		if (usr->inMsg->type == protocol::type::Password)
		{
			const protocol::Password *msg = static_cast<protocol::Password*>(usr->inMsg);
			
			hash.Update(reinterpret_cast<uint8_t*>(password), pw_len);
			hash.Update(reinterpret_cast<uint8_t*>(usr->seed), 4);
			hash.Final();
			char digest[protocol::password_hash_size];
			hash.GetHash(reinterpret_cast<uint8_t*>(digest));
			hash.Reset();
			
			if (memcmp(digest, msg->data, protocol::password_hash_size) != 0)
			{
				// mismatch, send error or disconnect.
				uRemove(usr, protocol::user_event::Dropped);
				break;
			}
			
			#ifdef CHECK_VIOLATIONS
			fSet(usr->tags, uTag::CanChange);
			#endif
			
			uSendMsg(usr, msgAck(msg->session_id, msg->type));
			
			// make sure the same seed is not used for something else.
			uRegenSeed(usr);
		}
		else
		{
			// not a password
			uRemove(usr, protocol::user_event::Violation);
			break;
		}
		
		usr->state = uState::login;
		uSendMsg(usr, msgHostInfo());
		
		break;
	case uState::init:
		if (usr->inMsg->type == protocol::type::Identifier)
		{
			const protocol::Identifier *ident = static_cast<protocol::Identifier*>(usr->inMsg);
			
			if (memcmp(ident->identifier, protocol::identifier_string, protocol::identifier_size) != 0)
			{
				#ifndef NDEBUG
				std::cerr << "Protocol string mismatch" << std::endl;
				#endif
				
				uRemove(usr, protocol::user_event::Violation);
				break;
			}
			
			if (ident->revision != protocol::revision)
			{
				#ifndef NDEBUG
				std::cerr << "Protocol revision mismatch ---  "
					<< "expected: " << protocol::revision
					<< ", got: " << ident->revision << std::endl;
				#endif
				
				// TODO: Implement some compatible way of announcing incompatibility
				
				uRemove(usr, protocol::user_event::Dropped);
				break;
			}
			
			if (password == 0)
			{
				// no password set
				
				#ifdef CHECK_VIOLATIONS
				fSet(usr->tags, uTag::CanChange);
				#endif
				
				usr->state = uState::login;
				uSendMsg(usr, msgHostInfo());
			}
			else
			{
				#ifndef NDEBUG
				std::cout << "Request password" << std::endl;
				#endif
				
				usr->state = uState::login_auth;
				uSendMsg(usr, msgAuth(usr, protocol::Global));
			}
			
			usr->level = ident->level; // feature level used by client
			usr->setCapabilities(ident->flags);
			usr->setExtensions(ident->extensions);
			usr->setAMode(default_user_mode);
			if (!usr->ext_chat)
				usr->a_deaf = true;
		}
		else
		{
			#ifndef NDEBUG
			std::cerr << "Invalid data from user #"
				<< static_cast<int>(usr->id) << std::endl;
			#endif
			
			uRemove(usr, protocol::user_event::Dropped);
		}
		break;
	case uState::dead:
		std::cerr << "I see dead people." << std::endl;
		uRemove(usr, protocol::user_event::Dropped);
		break;
	default:
		assert(!"user state was something strange");
		break;
	}
}

void Server::Propagate(Session* session, message_ref msg, User* source, const bool toAll) throw()
{
	#if defined(DEBUG_SERVER) and !defined(NDEBUG)
	std::cout << "Server::Propagate(session: " << static_cast<int>(msg->session_id)
		<< ", type: " << static_cast<int>(msg->type) << ")" << std::endl;
	#endif
	
	assert(session != 0);
	
	// Send ACK for the message we're about to share..
	if (source != 0)
	{
		uSendMsg(source, msgAck(session->id, msg->type));
	}
	
	User *usr_ptr;
	for (session_usr_iterator ui(session->users.begin()); ui != session->users.end(); ++ui)
	{
		if (ui->second != source)
		{
			usr_ptr = ui->second;
			uSendMsg(usr_ptr, msg);
		}
	}
	
	if (toAll)
	{
		// send to users waiting sync as well.
		for (userlist_iterator wui(session->waitingSync.begin()); wui != session->waitingSync.end(); ++wui)
		{
			if ((*wui) != source)
			{
				usr_ptr = *wui;
				uSendMsg(usr_ptr, msg);
			}
		}
	}
}

void Server::uSendMsg(User* usr, message_ref msg) throw()
{
	assert(usr != 0);
	
	#if defined(DEBUG_SERVER) and !defined(NDEBUG)
	std::cout << "Server::uSendMsg(to user: " << static_cast<int>(usr->id)
		<< ", type: " << static_cast<int>(msg->type) << ")" << std::endl;
	protocol::msgName(msg->type);
	#endif
	
	switch (msg->type)
	{
	case protocol::type::Chat:
		if (!usr->ext_chat or usr->a_deaf) // TODO: Check the correct session for deaf flag
			return;
		break;
	case protocol::type::Palette:
		if (!usr->ext_palette)
			return;
		break;
	default:
		// do nothing
		break;
	}
	
	usr->queue.push_back( msg );
	
	if (!fIsSet(usr->events, ev.write))
	{
		fSet(usr->events, ev.write);
		ev.modify(usr->sock->fd(), usr->events);
	}
}

void Server::SyncSession(Session* session) throw()
{
	assert(session != 0);
	
	#if defined(DEBUG_SERVER) and !defined(NDEBUG)
	std::cout << "Server::SyncSession(session: "
		<< static_cast<int>(session->id) << ")" << std::endl;
	#endif
	
	assert(session->syncCounter == 0);
	
	// TODO: Need better source user selection.
	const session_usr_iterator sui(session->users.begin());
	assert(sui != session->users.end());
	User* src(sui->second);
	
	// request raster
	message_ref sync_ref(new protocol::Synchronize);
	sync_ref->session_id = session->id;
	uSendMsg(src, sync_ref);
	
	// Release clients from syncwait...
	Propagate(session, msgAck(session->id, protocol::type::SyncWait));
	
	std::list<User*> newc;
	// Get new clients
	while (session->waitingSync.size() != 0)
	{
		newc.insert(newc.end(), session->waitingSync.front());
		session->waitingSync.pop_front();
	}
	
	protocol::SessionSelect *select; // warning from compiler
	
	std::vector<message_ref> msg_queue;
	// build msg_queue
	User *usr_ptr;
	for (session_usr_iterator old(session->users.begin()); old != session->users.end(); ++old)
	{
		// clear syncwait 
		usr_ptr = old->second;
		const usr_session_iterator usi(usr_ptr->sessions.find(session->id));
		if (usi != usr_ptr->sessions.end())
		{
			usi->second->syncWait = false;
		}
		
		// add messages to msg_queue
		msg_queue.push_back(msgUserEvent(usr_ptr, session, protocol::user_event::Join));
		if (usr_ptr->session->id == session->id)
		{
			select = new protocol::SessionSelect;
			select->user_id = usr_ptr->id;
			select->session_id = session->id;
			msg_queue.push_back(message_ref(select));
			
			if (usr_ptr->layer != protocol::null_layer)
			{
				message_ref layer_ref(new protocol::LayerSelect(usr_ptr->layer));
				layer_ref->user_id = usr_ptr->id;
				layer_ref->session_id = session->id;
				msg_queue.push_back(layer_ref);
			}
		}
	}
	
	userlist_iterator new_i(newc.begin()), new_i2;
	msgvector_iterator msg_queue_i;
	// Send messages
	for (; new_i != newc.end(); ++new_i)
	{
		for (msg_queue_i=msg_queue.begin(); msg_queue_i != msg_queue.end(); ++msg_queue_i)
		{
			uSendMsg(*new_i, *msg_queue_i);
		}
	}
	
	message_ref sev_ref;
	if (session->locked)
	{
		sev_ref.reset(new protocol::SessionEvent(protocol::session_event::Lock, protocol::null_user, 0));
	}
	
	// put waiting clients to normal data propagation and create raster tunnels.
	for (new_i = newc.begin(); new_i != newc.end(); ++new_i)
	{
		// Create fake tunnel
		tunnel.insert( std::make_pair(src->sock->fd(), (*new_i)->sock->fd()) );
		
		// add user to normal data propagation.
		session->users[(*new_i)->id] = (*new_i);
		
		// Tell other syncWait users of this user..
		if (newc.size() > 1)
		{
			for (new_i2 = newc.begin(); new_i2 != newc.end(); ++new_i2)
			{
				if (new_i2 == new_i) continue; // skip self
				uSendMsg(
					(*new_i),
					msgUserEvent((*new_i2), session, protocol::user_event::Join)
				);
			}
		}
		
		if (session->locked)
		{
			uSendMsg(*new_i, sev_ref);
		}
	}
}

void Server::uJoinSession(User* usr, Session* session) throw()
{
	assert(usr != 0);
	assert(session != 0);
	
	#if defined(DEBUG_SERVER) and !defined(NDEBUG)
	std::cout << "Server::uJoinSession(session: "
		<< static_cast<int>(session->id) << ")" << std::endl;
	#endif
	
	// Add session to users session list.
	usr->sessions[session->id] = new SessionData(session);
	assert(usr->sessions[session->id]->session != 0);
	
	// Remove locked and mute, if the user is the session's owner.
	if (session->owner == usr->id)
	{
		usr->sessions[session->id]->locked = false;
		usr->sessions[session->id]->muted = false;
	}
	// Remove mute if the user is server admin.
	else if (usr->isAdmin)
		usr->sessions[session->id]->muted = false;
	
	// Tell session members there's a new user.
	Propagate(session, msgUserEvent(usr, session, protocol::user_event::Join));
	
	if (session->users.size() != 0)
	{
		// put user to wait sync list.
		session->waitingSync.push_back(usr);
		usr->syncing = session->id;
		
		// don't start new client sync if one is already in progress...
		if (session->syncCounter == 0)
		{
			// Put session to syncing state
			session->syncCounter = session->users.size();
			
			// tell session users to enter syncwait state.
			Propagate(session, msgSyncWait(session));
		}
	}
	else
	{
		// session is empty
		session->users[usr->id] = usr;
		
		message_ref raster_ref(new protocol::Raster(0, 0, 0, 0));
		raster_ref->session_id = session->id;
		
		uSendMsg(usr, raster_ref);
	}
}

void Server::uLeaveSession(User* usr, Session*& session, const uint8_t reason) throw()
{
	assert(usr != 0);
	assert(session != 0);
	
	#if defined(DEBUG_SERVER) and !defined(NDEBUG)
	std::cout << "Server::uLeaveSession(user: "
		<< static_cast<int>(usr->id) << ", session: "
		<< static_cast<int>(session->id) << ")" << std::endl;
	#endif
	
	session->users.erase(usr->id);
	
	// remove
	delete usr->sessions.find(session->id)->second;
	usr->sessions.erase(session->id);
	
	// last user in session.. destruct it
	if (session->users.empty())
	{
		if (session->SelfDestruct)
		{
			freeSessionID(session->id);
			sessions.erase(session->id);
			delete session;
			session = 0;
		}
	}
	else
	{
		// Cancel raster sending for this user
		User* usr_ptr;
		tunnelmap_iterator tunnel_i(tunnel.begin());
		while (tunnel_i != tunnel.end())
		{
			if (tunnel_i->second == usr->sock->fd())
			{
				// In case we've not cleaned the tunnel properly from dead users.
				assert(users.find(tunnel_i->first) != users.end());
				
				usr_ptr = users[tunnel_i->first];
				message_ref cancel_ref(new protocol::Cancel);
				// TODO: Figure out which session it is from
				//cancel->session_id = session->id;
				uSendMsg(usr_ptr, cancel_ref);
				
				tunnel.erase(tunnel_i);
				// iterator was invalidated
				tunnel_i = tunnel.begin();
			}
			else
				++tunnel_i;
		}
		
		// Tell session members the user left
		if (reason != protocol::user_event::None)
		{
			Propagate(session, msgUserEvent(usr, session, reason));
			
			if (session->owner == usr->id)
			{
				session->owner = protocol::null_user;
				
				// Announce owner disappearance.
				message_ref sev_ref(
					new protocol::SessionEvent(
						protocol::session_event::Delegate, session->owner, 0
					)
				);
				sev_ref->session_id = session->id;
				Propagate(session, sev_ref);
			}
		}
	}
}

void Server::uAdd(Socket* sock) throw(std::bad_alloc)
{
	if (sock == 0)
	{
		#if defined(DEBUG_SERVER) and !defined(NDEBUG)
		std::cout << "Null socket, aborting user creation." << std::endl;
		#endif
		
		return;
	}
	
	// Check duplicate connections (should be enabled with command-line switch instead)
	#ifdef NO_DUPLICATE_CONNECTIONS
	for (usermap_iterator ui(users.begin()); ui != users.end(); ++ui)
	{
		if (sock->matchAddress(ui->second->sock))
		{
			#ifndef NDEBUG
			std::cout << "Multiple connections from: " << sock->address() << std::endl;
			#endif // NDEBUG
			
			delete sock;
			return;
		}
	}
	#endif // NO_DUPLICATE_CONNECTIONS
	
	const uint8_t id = getUserID();
	
	if (id == protocol::null_user)
	{
		#ifndef NDEBUG
		std::cout << "Server full, disconnecting user: " << sock->address() << std::endl;
		#endif
		
		delete sock;
		return;
	}
	
	std::cout << "New user #" << static_cast<int>(id)
		<< ", from: " << sock->address() << std::endl;
	
	User* usr = new User(id, sock);
	
	fSet(usr->events, ev.read);
	ev.add(usr->sock->fd(), usr->events);
	
	const size_t ts = utimer.size();
	
	if (ts > 20)
		time_limit = srv_defaults::time_limit / 6; // half-a-minute
	else if (ts > 10)
		time_limit = srv_defaults::time_limit / 3; // 1 minute
	else
		time_limit = srv_defaults::time_limit; // 3 minutes
	
	usr->deadtime = time(0) + time_limit;
	
	// re-schedule user culling
	if (next_timer > usr->deadtime)
		next_timer = usr->deadtime + 1;
	
	// add user to timer
	utimer.insert(utimer.end(), usr);
	
	const size_t nwbuffer = 4096;
	
	usr->input.setBuffer(new char[nwbuffer], nwbuffer);
	usr->output.setBuffer(new char[nwbuffer], nwbuffer);
	
	users[sock->fd()] = usr;
	
	#if defined(DEBUG_SERVER) and !defined(NDEBUG)
	std::cout << "Known users: " << users.size() << std::endl;
	#endif
}

void Server::breakSync(User* usr) throw()
{
	assert(usr != 0);
	
	#if defined(DEBUG_SERVER) and !defined(NDEBUG)
	std::cout << "Server::breakSync(user: "
		<< static_cast<int>(usr->id) << ")" << std::endl;
	#endif
	
	uSendMsg(usr, msgError(usr->syncing, protocol::error::SyncFailure));
	
	const sessionmap_iterator sui(sessions.find(usr->syncing));
	if (sui == sessions.end())
	{
		#if defined(DEBUG_SERVER) and !defined(NDEBUG)
		std::cerr << "Session to break sync with was not found!" << std::endl;
		#endif
		
		return;
	}
	
	uLeaveSession(usr, sui->second);
	usr->syncing = protocol::Global;
}

void Server::uRemove(User*& usr, const uint8_t reason) throw()
{
	assert(usr != 0);
	
	#if defined(DEBUG_SERVER) and !defined(NDEBUG)
	std::cout << "Server::uRemove(user: " << static_cast<int>(usr->id)
		<< ", at address: " << usr->sock->address() << ")" << std::endl;
	#endif
	
	usr->state = uState::dead;
	
	// Remove from event system
	ev.remove(usr->sock->fd(), ev.read|ev.write|ev.error|ev.hangup);
	
	// Clear the fake tunnel of any possible instance of this user.
	// We're the source...
	tunnelmap_iterator ti;
	usermap_iterator usi;
	while ((ti = tunnel.find(usr->sock->fd())) != tunnel.end())
	{
		usi = users.find(ti->second);
		if (usi == users.end())
		{
			#if defined(DEBUG_SERVER) and !defined(NDEBUG)
			std::cerr << "Tunnel's other end was not found in users." << std::endl;
			#endif
			continue;
		}
		breakSync(usi->second);
		tunnel.erase(ti);
	}
	
	// break tunnels
	ti = tunnel.begin();
	//std::set<fd_t> sources;
	while (ti != tunnel.end())
	{
		if (ti->second == usr->sock->fd())
		{
			//sources.insert(sources.end(), ti->first);
			
			tunnel.erase(ti);
			// iterator was invalidated
			ti = tunnel.begin();
		}
		else
			++ti;
	}
	
	/*
	// cancel target-less rasters
	if (!sources.empty())
	{
		User *src_usr;
		message_ref cancel_ref(new protocol::Cancel);
		cancel_ref->session_id = session->id;
		
		for (std::set<User*>::iterator src_i(sources.begin()); src_i != sources.end(); src_i++)
		{
			ti = tunnel.find(*src_i);
			
			if (ti == tunnel.end())
			{
				User *src_usr
				usi = users.find(ti->first);
				if (usi != users.end())
				{
					src_usr = usi->second;
					uSendMsg(src_usr, cancel_ref);
				}
			}
		}
	}
	*/
	
	// clean sessions
	Session *session;
	while (usr->sessions.size() != 0)
	{
		session = usr->sessions.begin()->second->session;
		uLeaveSession(usr, session, reason);
	}
	
	// remove from fd -> User* map
	users.erase(usr->sock->fd());
	
	freeUserID(usr->id);
	
	// clear any idle timer associated with this user.
	utimer.erase(usr);
	
	delete usr;
	usr = 0;
	
	// Transient mode exit.
	if (Transient and users.empty())
	{
		state = server::state::Exiting;
	}
}

bool Server::init() throw(std::bad_alloc)
{
	assert(state == server::state::None);
	
	srand(time(0) - 513); // FIXME
	
	if (lsock.create() == INVALID_SOCKET)
	{
		std::cerr << "! Failed to create a socket." << std::endl;
		return false;
	}
	
	lsock.block(false); // nonblocking
	lsock.reuse(true); // reuse address
	
	bool bound = false;
	for (uint16_t bport=lo_port; bport != hi_port+1; ++bport)
	{
		#ifdef IPV6_SUPPORT
		if (lsock.bindTo("::", bport) == SOCKET_ERROR)
		#else
		if (lsock.bindTo("0.0.0.0", bport) == SOCKET_ERROR)
		#endif
		{
			const int bind_err = lsock.getError();
			if (bind_err == EBADF or bind_err == EINVAL)
				return false;
			
			// continue
		}
		else
		{
			bound = true;
			break;
		}
	}
	
	if (!bound)
	{
		std::cerr << "Failed to bind to any port" << std::endl;
		return false;
	}
	
	if (lsock.listen() == SOCKET_ERROR)
	{
		std::cerr << "Failed to open listening port." << std::endl;
		return false;
	}
	
	std::cout << "Listening on: " << lsock.address() << std::endl;
	
	if (!ev.init())
	{
		std::cerr << "Event system initialization failed." << std::endl;
		return false;
	}
	
	ev.add(lsock.fd(), ev.read);
	
	state = server::state::Init;
	
	return true;
}

bool Server::validateUserName(User* usr) const throw()
{
	assert(usr != 0);
	
	if (!fIsSet(requirements, protocol::requirements::EnforceUnique))
		return true;
	
	if (usr->nlen == 0)
	{
		#ifndef NDEBUG
		std::cerr << "Zero length user name." << std::endl;
		#endif
		
		return false;
	}
	
	for (usermap_const_iterator ui(users.begin()); ui != users.end(); ++ui)
	{
		if (ui->second == usr) continue; // skip self
		
		if (usr->nlen == ui->second->nlen
			and (memcmp(usr->name, ui->second->name, usr->nlen) == 0))
		{
			#ifndef NDEBUG
			std::cerr << "Name conflicts with user #"
				<< static_cast<int>(ui->second->id) << std::endl;
			#endif
			return false;
		}
	}
	
	return true;
}

bool Server::validateSessionTitle(Session* session) const throw()
{
	assert(session != 0);
	
	// Session title is always unique if name enforcing is not enabled.
	if (!fIsSet(requirements, protocol::requirements::EnforceUnique))
		return true;
	
	// Session title is never unique if it's an empty string.
	if (session->len == 0) return false;
	
	for (sessionmap_const_iterator si(sessions.begin()); si != sessions.end(); ++si)
	{
		if (si->second == session) continue; // skip self
		
		if (session->len == si->second->len
			and (memcmp(session->title, si->second->title, session->len) == 0))
		{
			return false;
		}
	}
	
	return true;
}

void Server::cullIdlers() throw()
{
	User *usr;
	for (std::set<User*>::iterator tui(utimer.begin()); tui != utimer.end(); ++tui)
	{
		if ((*tui)->deadtime < current_time)
		{
			#ifndef NDEBUG
			std::cout << "Killing idle user: "
				<< static_cast<int>((*tui)->id) << std::endl;
			#endif
			
			usr = *tui;
			--tui;
			
			uRemove(usr, protocol::user_event::TimedOut);
		}
		else if ((*tui)->deadtime < next_timer)
		{
			// re-schedule next culling to come sooner
			next_timer = (*tui)->deadtime;
		}
	}
}
