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
#ifndef DP_NET_USERLISTMODEL_H
#define DP_NET_USERLISTMODEL_H

#include <QAbstractTableModel>
#include <QSortFilterProxyModel>
#include <QList>
#include <QPixmap>

class QJsonArray;

namespace protocol { class MessagePtr; }

namespace canvas {

/**
 * @brief Information about a user
 */
struct User {
	int id;
	QString name;
	QPixmap avatar;
	bool isLocal;
	bool isOperator;
	bool isTrusted;
	bool isMod;
	bool isBot;
	bool isAuth;
	bool isLocked;
	bool isMuted;
	bool isOnline;
};

class OnlineUserListModel;

/**
 * A list model to represent session users.
 */
class UserListModel : public QAbstractTableModel {
	Q_OBJECT
public:
	enum UserListRoles {
		IdRole = Qt::UserRole + 1,
		NameRole,
		AvatarRole,
		IsOpRole,
		IsTrustedRole,
		IsModRole,
		IsAuthRole,
		IsBotRole,
		IsLockedRole,
		IsMutedRole,
		IsOnlineRole
	};

	UserListModel(QObject *parent=nullptr);

	QVariant data(const QModelIndex& index, int role=Qt::DisplayRole) const override;
	int rowCount(const QModelIndex& parent=QModelIndex()) const override;
	int columnCount(const QModelIndex& parent=QModelIndex()) const override;
	QVariant headerData(int section, Qt::Orientation orientation, int role=Qt::DisplayRole) const override;

	//! A new user logs in
	void userLogin(const User &user);

	//! A user logs out (marked as offline)
	void userLogout(int id);

	//! Mark all users as offline
	void allLogout();

	//! Clear all users and history
	void reset();

	//! Get all users (includes logged out users)
	const QVector<User> &users() const { return m_users; }

	//! Get a shared instance of a filtered model that only lists online users
	OnlineUserListModel *onlineUsers() const { return m_onlineUsers; }

	/**
	 * @brief Get user info by ID
	 *
	 * This will return info about past users as well.
	 * @param id user id
	 * @return
	 */
	User getUserById(int id) const;

	/**
	 * @brief Get the name of the user with the given context ID
	 *
	 * If no such user exists, "User #X" is returned, where X is the ID number.
	 * @param id
	 * @return user name
	 */
	QString getUsername(int id) const;

	//! Get a list of users with operator privileges
	QList<uint8_t> operatorList() const;

	//! Get a list of users who are locked
	QList<uint8_t> lockList() const;

	//! Get a list of trusted users
	QList<uint8_t> trustedList() const;

	/**
	 * @brief Get the command for (un)locking a single user
	 * @param localId
	 * @param userId
	 * @param lock
	 * @return
	 */
	protocol::MessagePtr getLockUserCommand(int localId, int userId, bool lock) const;

	/**
	 * @brief Get the command for granting or revoking operator privileges
	 * @param localId
	 * @param userId
	 * @param op
	 * @return
	 */
	protocol::MessagePtr getOpUserCommand(int localId, int userId, bool op) const;

	/**
	 * @brief Get the command for granting or revoking trusted status
	 * @param localId
	 * @param userId
	 * @param trusted
	 * @return
	 */
	protocol::MessagePtr getTrustUserCommand(int localId, int userId, bool op) const;

public slots:
	void updateOperators(const QList<uint8_t> operatorIds);
	void updateTrustedUsers(const QList<uint8_t> trustedIds);
	void updateLocks(const QList<uint8_t> lockedUserIds);
	void updateMuteList(const QJsonArray &mutedUserIds);

private:
	QVector<User> m_users;
	OnlineUserListModel *m_onlineUsers;
};

/**
 * A filtered user list model that only includes online users
 */
class OnlineUserListModel : public QSortFilterProxyModel
{
	Q_OBJECT
public:
	using QSortFilterProxyModel::QSortFilterProxyModel;

protected:
	bool filterAcceptsRow(int source_row, const QModelIndex &source_parent) const;
};

}

Q_DECLARE_METATYPE(canvas::User)

#endif

