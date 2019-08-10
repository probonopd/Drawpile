/*
   Drawpile - a collaborative drawing program.

   Copyright (C) 2007-2019 Calle Laakkonen

   Drawpile is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   Drawpile is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with Drawpile.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "userlist.h"
#include "utils/icon.h"
#include "../libshared/net/meta.h"
#include "../libshared/net/meta2.h"

#include <QDebug>
#include <QJsonArray>
#include <QPixmap>
#include <QPalette>

namespace canvas {

UserListModel::UserListModel(QObject *parent)
	: QAbstractTableModel(parent)
{
	m_onlineUsers = new OnlineUserListModel(this);
	m_onlineUsers->setSourceModel(this);
}


QVariant UserListModel::data(const QModelIndex& index, int role) const
{
	if(!index.isValid() || index.row() < 0 || index.row() >= m_users.size())
		return QVariant();

	const User &u = m_users.at(index.row());

	if(role == Qt::ForegroundRole && !u.isOnline) {
		return QPalette().color(QPalette::ColorGroup::Disabled, QPalette::ColorRole::Foreground);
	}

	if(index.column() == 0) {
		switch(role) {
			case IdRole: return u.id;
			case Qt::DisplayRole:
			case NameRole: return u.name;
			case Qt::DecorationRole:
			case AvatarRole: return u.avatar;
			case IsOpRole: return u.isOperator;
			case IsTrustedRole: return u.isTrusted;
			case IsModRole: return u.isMod;
			case IsAuthRole: return u.isAuth;
			case IsBotRole: return u.isBot;
			case IsLockedRole: return u.isLocked;
			case IsMutedRole: return u.isMuted;
			case IsOnlineRole: return u.isOnline;
		}

	} else if(role==Qt::DisplayRole) {
		switch(index.column()) {
		case 1:
			if(u.isMod)
				return tr("Moderator");
			else if(u.isOperator)
				return tr("Operator");
			else if(u.isTrusted)
				return tr("Trusted");
			else if(u.isAuth)
				return tr("Registered");
			else
				return QVariant();

		case 2: return u.isOnline ? tr("Online") : tr("Offline");
		}

	} else if(role==Qt::DecorationRole) {
		switch(index.column()) {
		case 3: return u.isLocked ? icon::fromTheme("object-locked") : QVariant();
		case 4: return u.isMuted ? icon::fromTheme("irc-unvoice") : QVariant();
		}
	}

	return QVariant();
}

int UserListModel::columnCount(const QModelIndex&) const
{
	return 5;
}

int UserListModel::rowCount(const QModelIndex& parent) const
{
	if(parent.isValid())
		return 0;
	return m_users.count();
}

QVariant UserListModel::headerData(int section, Qt::Orientation orientation, int role) const
{
	if(orientation == Qt::Horizontal) {
		if(role == Qt::DisplayRole) {
			switch(section) {
			case 0: return tr("User");
			case 1: return tr("Type");
			case 2: return tr("Status");
			}
		} else if(role == Qt::DecorationRole) {
			switch(section) {
			case 3: return icon::fromTheme("object-locked");
			case 4: return icon::fromTheme("irc-unvoice");
			}
		}

	} else if(section >=0 && section < m_users.size() && role == Qt::DisplayRole) {
		return m_users.at(section).id;
	}

	return QVariant();
}

void UserListModel::userLogin(const User &user)
{
	// Check if this is a returning user
	for(int i=0;i<m_users.count();++i) {
		User &u = m_users[i];
		if(u.id == user.id) {
			u.name = user.name;
			u.avatar = user.avatar;
			u.isLocal = user.isLocal;
			u.isAuth = user.isAuth;
			u.isMod = user.isMod;
			u.isBot = user.isBot;
			u.isMuted = user.isMuted;
			u.isOnline = true;

			emit dataChanged(index(i, 0), index(i, columnCount()-1));
			return;
		}
	}

	// Add new user
	int pos = m_users.count();
	beginInsertRows(QModelIndex(),pos,pos);
	m_users.append(user);
	endInsertRows();
}

void UserListModel::userLogout(int id)
{
	for(int i=0;i<m_users.size();++i) {
		if(m_users.at(i).id == id) {
			m_users[i].isOnline = false;
			emit dataChanged(index(i, 0), index(i, columnCount()-1));
			return;
		}
	}
}

void UserListModel::allLogout()
{
	if(!m_users.isEmpty()) {
		for(int i=0;i<m_users.size();++i)
			m_users[i].isOnline = false;
		emit dataChanged(
			index(0, 0),
			index(m_users.size()-1, columnCount()-1)
		);
	}
}

void UserListModel::reset()
{
	beginRemoveRows(QModelIndex(), 0, m_users.size()-1);
	m_users.clear();
	endRemoveRows();
}

void UserListModel::updateOperators(const QList<uint8_t> ids)
{
	for(int i=0;i<m_users.size();++i) {
		User &u = m_users[i];

		const bool op = ids.contains(u.id);
		if(op != u.isOperator) {
			u.isOperator = op;
			emit dataChanged(index(i, 0), index(i, columnCount()-1));
		}
	}
}

void UserListModel::updateTrustedUsers(const QList<uint8_t> trustedIds)
{
	for(int i=0;i<m_users.size();++i) {
		User &u = m_users[i];

		const bool trusted = trustedIds.contains(u.id);
		if(trusted != u.isTrusted) {
			u.isTrusted = trusted;
			emit dataChanged(index(i, 0), index(i, columnCount()-1));
		}
	}
}

void UserListModel::updateLocks(const QList<uint8_t> ids)
{
	for(int i=0;i<m_users.size();++i) {
		User &u = m_users[i];

		const bool lock = ids.contains(u.id);
		if(lock != u.isLocked) {
			u.isLocked = lock;
			emit dataChanged(index(i, 0), index(i, columnCount()-1));
		}
	}
}

void UserListModel::updateMuteList(const QJsonArray &mutedUserIds)
{
	for(int i=0;i<m_users.size();++i) {
		User &u = m_users[i];
		const bool mute = mutedUserIds.contains(u.id);
		if(u.isMuted != mute) {
			u.isMuted = mute;
			emit dataChanged(index(i, 0), index(i, columnCount()-1));
		}
	}
}

QList<uint8_t> UserListModel::operatorList() const
{
	QList<uint8_t> ops;
	for(int i=0;i<m_users.size();++i) {
		const User &u = m_users.at(i);
		if(u.isOnline && (u.isOperator || u.isMod))
			ops << u.id;
	}
	return ops;
}

QList<uint8_t> UserListModel::lockList() const
{
	QList<uint8_t> locks;
	for(int i=0;i<m_users.size();++i) {
		const User &u = m_users.at(i);
		if(u.isOnline && u.isLocked)
			locks << m_users.at(i).id;
	}
	return locks;
}

QList<uint8_t> UserListModel::trustedList() const
{
	QList<uint8_t> ids;
	for(int i=0;i<m_users.size();++i) {
		const User &u = m_users.at(i);
		if(u.isOnline && u.isTrusted)
			ids << m_users.at(i).id;
	}
	return ids;
}

User UserListModel::getUserById(int id) const
{
	for(const User &u : m_users)
		if(u.id == id)
			return u;

	// Nothing found
	return User();
}

QString UserListModel::getUsername(int id) const
{
	// Special case: id 0 is reserved for the server
	if(id==0)
		return tr("Server");

	for(const User &u : m_users)
		if(u.id == id)
			return u.name;

	// Not found
	return tr("User #%1").arg(id);
}

protocol::MessagePtr UserListModel::getLockUserCommand(int localId, int userId, bool lock) const
{
	Q_ASSERT(userId>0 && userId<255);

	QList<uint8_t> ids = lockList();
	if(lock) {
		if(!ids.contains(userId))
			ids.append(userId);
	} else {
		ids.removeAll(userId);
	}

	return protocol::MessagePtr(new protocol::UserACL(localId, ids));
}

protocol::MessagePtr UserListModel::getOpUserCommand(int localId, int userId, bool op) const
{
	Q_ASSERT(userId>0 && userId<255);

	QList<uint8_t> ops = operatorList();
	if(op) {
		if(!ops.contains(userId))
			ops.append(userId);
	} else {
		ops.removeOne(userId);
	}

	return protocol::MessagePtr(new protocol::SessionOwner(localId, ops));
}

protocol::MessagePtr UserListModel::getTrustUserCommand(int localId, int userId, bool trust) const
{
	Q_ASSERT(userId>0 && userId<255);

	QList<uint8_t> trusted = trustedList();
	if(trust) {
		if(!trusted.contains(userId))
			trusted.append(userId);
	} else {
		trusted.removeOne(userId);
	}

	return protocol::MessagePtr(new protocol::TrustedUsers(localId, trusted));
}

bool OnlineUserListModel::filterAcceptsRow(int source_row, const QModelIndex &parent) const
{
	const auto i = sourceModel()->index(source_row, 0);
	return i.data(UserListModel::IsOnlineRole).toBool() && QSortFilterProxyModel::filterAcceptsRow(source_row, parent);
}

}
