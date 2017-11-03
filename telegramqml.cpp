/*
    Copyright (C) 2014 Aseman
    http://aseman.co

    This project is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This project is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "telegramqml.h"
#include "userdata.h"
#include "database.h"
#include "telegramsearchmodel.h"
#include "telegrammessagesmodel.h"
#include "telegramthumbnailer.h"
#include "objects/types.h"
#include "utils.h"
#include <secret/decrypter.h>
#include <util/utils.h>
#include <telegram.h>

#include <limits>

#include <QPointer>
#include <QTimerEvent>
#include <QDebug>
#include <QFile>
#include <QHash>
#include <QImage>
#include <QDateTime>
#include <QMimeDatabase>
#include <QMimeType>
#include <QImage>
#include <QImageReader>
#include <QImageWriter>
#include <QBuffer>
#include <QTimer>
#include <QAudioDecoder>
#include <QMediaMetaData>
#include <QCryptographicHash>

#ifdef Q_OS_WIN
#define FILES_PRE_STR QString("file:///")
#else
#define FILES_PRE_STR QString("file://")
#endif

#ifdef UBUNTU_PHONE
#include <stdexcept>
#include <QSize>
#include <QSharedPointer>
#include "thumbnailer-qt.h"

const int THUMB_SIZE = 128;

#endif


TelegramQmlPrivate *telegramp_qml_tmp = 0;
bool checkDialogLessThan( qint64 a, qint64 b );
bool checkMessageLessThan( qint64 a, qint64 b );

class TelegramQmlPrivate
{
public:
    QString defaultHostAddress;
    int defaultHostPort;
    int defaultHostDcId;
    int appId;
    QString appHash;

    UserData *userdata;
    Database *database;
    Telegram *telegram;
    Settings *tsettings;

    DatabaseAbstractEncryptor *encrypter;

    QString phoneNumber;
    QString downloadPath;
    QString tempPath;
    QString configPath;
    QUrl publicKeyFile;
    QString currentSalt;

    bool globalMute;
    bool online;
    bool invisible;
    int unreadCount;
    int autoRewakeInterval;
    qreal totalUploadedPercent;

    bool autoAcceptEncrypted;
    bool autoCleanUpMessages;

    bool authNeeded;
    bool authLoggedIn;
    bool phoneRegistered;
    bool phoneInvited;
    bool phoneChecked;

    QString authSignInCode;
    QString authSignUpError;
    QString authSignInError;
    int authCheckPhoneRetry;
    QString error;

    bool loggingOut;
    qint64 logout_req_id;
    qint64 checkphone_req_id;
    QHash<qint64,QString> phoneCheckIds;
    qint64 profile_upload_id;
    QString upload_photo_path;

    QSet<TelegramMessagesModel*> messagesModels;
    QSet<TelegramSearchModel*> searchModels;

    QHash<qint64,DialogObject*> dialogs;
    QHash<qint64,MessageObject*> messages;
    QHash<qint64,ChatObject*> chats;
    QHash<qint64,UserObject*> users;
    QHash<QString,StickerPackObject*> stickerPacks;
    QHash<qint64,StickerSetObject*> stickerSets;
    QHash<qint64,DocumentObject*> documents;
    QHash<qint64,ChatFullObject*> chatfulls;
    QHash<qint64,ContactObject*> contacts;
    QHash<qint64,EncryptedMessageObject*> encmessages;
    QHash<qint64,EncryptedChatObject*> encchats;
    QSet<UploadObject*> uploadPercents;

    QSet<qint64> stickers;
    QMap<qint64, QSet<qint64> > stickersMap;
    QSet<qint64> installedStickerSets;
    QHash<QString, qint64> stickerShortIds;

    QMultiMap<QString, qint64> userNameIndexes;

    QHash<qint64,DialogObject*> fakeDialogs;

    QList<qint64> dialogs_list;
    QHash<qint64, QList<qint64> > messages_list;
    QMap<qint64, WallPaperObject*> wallpapers_map;

    QHash<qint64,MessageObject*> pend_messages;
    QHash<qint64,FileLocationObject*> downloads;
    QHash<qint64,MessageObject*> uploads;
    QHash<qint64,FileLocationObject*> accessHashes;
    QHash<qint64,qint64> read_history_requests;
    QHash<qint64,qint64> delete_history_requests;
    QSet<qint64> deleteChatIds;
    QHash<qint64,qint64> blockRequests;
    QHash<qint64,qint64> unblockRequests;
    QList<qint64> request_messages;
    QMultiHash<qint64, qint64> pending_replies;
    QHash<qint64, QString> pending_stickers_uninstall;
    QHash<qint64, QString> pending_stickers_install;
    QHash<qint64, DocumentObject*> pending_doc_stickers;

    QSet<QObject*> garbages;

    QHash<int, QPair<qint64,qint64> > typing_timers;
    int upd_dialogs_timer;
    int update_contacts_timer;
    int garbage_checker_timer;

    DialogObject *nullDialog;
    MessageObject *nullMessage;
    ChatObject *nullChat;
    UserObject *nullUser;
    FileLocationObject *nullFile;
    WallPaperObject *nullWallpaper;
    UploadObject *nullUpload;
    ChatFullObject *nullChatFull;
    ContactObject *nullContact;
    FileLocationObject *nullLocation;
    EncryptedChatObject *nullEncryptedChat;
    EncryptedMessageObject *nullEncryptedMessage;
    DocumentObject *nullSticker;
    StickerSetObject *nullStickerSet;
    StickerPackObject *nullStickerPack;

    QMimeDatabase mime_db;

    qint32 msg_send_id_counter;
    qint64 msg_send_random_id;

    QPointer<QObject> newsletter_dlg;
    QTimer *cleanUpTimer;
    QTimer *messageRequester;

    UpdatesState state;

    TelegramThumbnailer thumbnailer;

    QTimer *sleepTimer;
    QTimer *wakeTimer;
};

TelegramQml::TelegramQml(QObject *parent) :
    QObject(parent)
{
    p = new TelegramQmlPrivate;
    p->defaultHostPort = 0;
    p->defaultHostDcId = 0;
    p->appId = 0;
    p->upd_dialogs_timer = 0;
    p->update_contacts_timer = 0;
    p->garbage_checker_timer = 0;
    p->unreadCount = 0;
    p->autoRewakeInterval = 0;
    p->encrypter = 0;
    p->totalUploadedPercent = 0;
    p->authCheckPhoneRetry = 0;
    p->online = false;
    p->globalMute = true;
    p->invisible = false;
    p->msg_send_id_counter = INT_MAX - 100000;
    p->msg_send_random_id = 0;
    p->newsletter_dlg = 0;
    p->sleepTimer = 0;
    p->wakeTimer = 0;
    p->autoAcceptEncrypted = false;
    p->autoCleanUpMessages = false;

    p->cleanUpTimer = new QTimer(this);
    p->cleanUpTimer->setSingleShot(true);
    p->cleanUpTimer->setInterval(60000);

    p->messageRequester = new QTimer(this);
    p->messageRequester->setSingleShot(true);
    p->messageRequester->setInterval(50);

    p->userdata = new UserData(this);
    p->database = new Database(this);

    p->telegram = 0;
    p->tsettings = 0;
    p->authNeeded = false;
    p->authLoggedIn = false;
    p->phoneRegistered = false;
    p->phoneInvited = false;
    p->phoneChecked = false;

    p->loggingOut = false;
    p->logout_req_id = 0;
    p->checkphone_req_id = 0;
    p->phoneCheckIds.clear();
    p->profile_upload_id = 0;

    p->nullDialog = new DialogObject( Dialog(), this );
    p->nullMessage = new MessageObject(Message(Message::typeMessageEmpty), this);
    p->nullChat = new ChatObject(Chat(Chat::typeChatEmpty), this);
    p->nullUser = new UserObject(User(User::typeUserEmpty), this);
    p->nullFile = new FileLocationObject(FileLocation(FileLocation::typeFileLocationUnavailable), this);
    p->nullWallpaper = new WallPaperObject(WallPaper(WallPaper::typeWallPaperSolid), this);
    p->nullUpload = new UploadObject(this);
    p->nullChatFull = new ChatFullObject(ChatFull(), this);
    p->nullContact = new ContactObject(Contact(), this);
    p->nullLocation = new FileLocationObject(FileLocation(), this);
    p->nullEncryptedChat = new EncryptedChatObject(EncryptedChat(), this);
    p->nullEncryptedMessage = new EncryptedMessageObject(EncryptedMessage(), this);
    p->nullSticker = new DocumentObject(Document(), this);
    p->nullStickerSet = new StickerSetObject(StickerSet(), this);
    p->nullStickerPack = new StickerPackObject(StickerPack(), this);

    connect(p->cleanUpTimer    , SIGNAL(timeout()), SLOT(cleanUpMessages_prv())   );
    connect(p->messageRequester, SIGNAL(timeout()), SLOT(requestReadMessage_prv()));
}

QString TelegramQml::phoneNumber() const
{
    return p->phoneNumber;
}

void TelegramQml::setPhoneNumber(const QString &phone)
{
    if( p->phoneNumber == phone )
        return;

    p->phoneNumber = phone;
    p->userdata->setPhoneNumber(phone);
    p->database->setPhoneNumber(phone);

    try_init();

    Q_EMIT phoneNumberChanged();
    Q_EMIT downloadPathChanged();
    Q_EMIT userDataChanged();
    Q_EMIT databaseChanged();

    connect(p->database, SIGNAL(chatFounded(Chat))         , SLOT(dbChatFounded(Chat))         );
    connect(p->database, SIGNAL(userFounded(User))         , SLOT(dbUserFounded(User))         );
    connect(p->database, SIGNAL(dialogFounded(Dialog,bool)), SLOT(dbDialogFounded(Dialog,bool)));
    connect(p->database, SIGNAL(messageFounded(Message))   , SLOT(dbMessageFounded(Message))   );
    connect(p->database, SIGNAL(contactFounded(Contact))   , SLOT(dbContactFounded(Contact))   );
    connect(p->database, SIGNAL(mediaKeyFounded(qint64,QByteArray,QByteArray)), SLOT(dbMediaKeysFounded(qint64,QByteArray,QByteArray)) );
}

QString TelegramQml::downloadPath() const
{
    return p->downloadPath + "/" + phoneNumber() + "/downloads";
}

void TelegramQml::setDownloadPath(const QString &downloadPath)
{
    if( p->downloadPath == downloadPath )
        return;

    p->downloadPath = downloadPath;
    Q_EMIT downloadPathChanged();
}

QString TelegramQml::tempPath() const
{
    return p->tempPath + "/" + phoneNumber() + "/temp";
}

void TelegramQml::setTempPath(const QString &tempPath)
{
    if( p->tempPath == tempPath)
        return;

    p->tempPath = tempPath;
    Q_EMIT tempPathChanged();
}

QString TelegramQml::homePath() const
{
    return QDir::homePath();
}

QString TelegramQml::currentPath() const
{
    return QDir::currentPath();
}

QString TelegramQml::configPath() const
{
    return p->configPath;
}

void TelegramQml::setConfigPath(const QString &conf)
{
    if( p->configPath == conf )
        return;

    p->configPath = conf;
    p->database->setConfigPath(conf);
    p->userdata->setConfigPath(conf);

    if( p->tempPath.isEmpty() )
        p->tempPath = conf;
    if( p->downloadPath.isEmpty() )
        p->downloadPath = conf;

    try_init();

    Q_EMIT configPathChanged();
    Q_EMIT tempPathChanged();
    Q_EMIT downloadPathChanged();
}

QString TelegramQml::publicKeyPath() const
{
    const QString &str = p->publicKeyFile.toString();

    QStringList list;
    list << p->publicKeyFile.toLocalFile();
    list << p->publicKeyFile.toString();
    if(str.left(4) == "qrc:")
    {
        list << str.mid(4);
        list << str.mid(3);
    }

    Q_FOREACH(const QString &l, list)
    {
        if(l.isEmpty())
            continue;
        if(QFileInfo::exists(l))
            return l;
    }

    return QString();
}

QUrl TelegramQml::publicKeyFile() const
{
    return p->publicKeyFile;
}

void TelegramQml::setPublicKeyFile(const QUrl &file)
{
    if( p->publicKeyFile == file )
        return;

    p->publicKeyFile = file;
    try_init();
    Q_EMIT publicKeyFileChanged();
}

void TelegramQml::setDefaultHostAddress(const QString &host)
{
    if(p->defaultHostAddress == host)
        return;

    p->defaultHostAddress = host;
    try_init();
    Q_EMIT defaultHostAddressChanged();
}

QString TelegramQml::defaultHostAddress() const
{
    return p->defaultHostAddress;
}

void TelegramQml::setDefaultHostPort(int port)
{
    if(p->defaultHostPort == port)
        return;

    p->defaultHostPort = port;
    try_init();
    Q_EMIT defaultHostPortChanged();
}

int TelegramQml::defaultHostPort() const
{
    return p->defaultHostPort;
}

void TelegramQml::setDefaultHostDcId(int dcId)
{
    if(p->defaultHostDcId == dcId)
        return;

    p->defaultHostDcId = dcId;
    try_init();
    Q_EMIT defaultHostDcIdChanged();
}

int TelegramQml::defaultHostDcId() const
{
    return p->defaultHostDcId;
}

void TelegramQml::setAppId(int appId)
{
    if(p->appId == appId)
        return;

    p->appId = appId;
    try_init();
    Q_EMIT appIdChanged();
}

int TelegramQml::appId() const
{
    return p->appId;
}

void TelegramQml::setAppHash(const QString &appHash)
{
    if(p->appHash == appHash)
        return;

    p->appHash = appHash;
    try_init();
    Q_EMIT appHashChanged();
}

QString TelegramQml::appHash() const
{
    return p->appHash;
}

void TelegramQml::setEncrypter(DatabaseAbstractEncryptor *encrypter)
{
    if(p->encrypter == encrypter)
        return;

    p->encrypter = encrypter;
    if(p->database)
        p->database->setEncrypter(p->encrypter);

    Q_EMIT encrypterChanged();
}

DatabaseAbstractEncryptor *TelegramQml::encrypter() const
{
    return p->encrypter;
}

void TelegramQml::setAutoAcceptEncrypted(bool stt)
{
    if(p->autoAcceptEncrypted == stt)
        return;

    p->autoAcceptEncrypted = stt;

    Q_EMIT autoAcceptEncryptedChanged();
}

bool TelegramQml::autoAcceptEncrypted() const
{
    return p->autoAcceptEncrypted;
}

void TelegramQml::setAutoCleanUpMessages(bool stt)
{
    if(p->autoCleanUpMessages == stt)
        return;

    p->autoCleanUpMessages = stt;
    if(p->autoCleanUpMessages)
        cleanUpMessages();

    Q_EMIT autoCleanUpMessagesChanged();
}

bool TelegramQml::autoCleanUpMessages() const
{
    return p->autoCleanUpMessages;
}

void TelegramQml::registerMessagesModel(TelegramMessagesModel *model)
{
    p->messagesModels.insert(model);
    connect(model, SIGNAL(dialogChanged()), this, SLOT(cleanUpMessages()));
}

void TelegramQml::unregisterMessagesModel(TelegramMessagesModel *model)
{
    p->messagesModels.remove(model);
    disconnect(model, SIGNAL(dialogChanged()), this, SLOT(cleanUpMessages()));
}

void TelegramQml::registerSearchModel(TelegramSearchModel *model)
{
    p->searchModels.insert(model);
}

void TelegramQml::unregisterSearchModel(TelegramSearchModel *model)
{
    p->searchModels.remove(model);
}

UserData *TelegramQml::userData() const
{
    return p->userdata;
}

Database *TelegramQml::database() const
{
    return p->database;
}

Telegram *TelegramQml::telegram() const
{
    return p->telegram;
}

qint64 TelegramQml::me() const
{
    if( p->telegram )
        return p->telegram->ourId();
    else
        return 0;
}

UserObject *TelegramQml::myUser() const
{
    return p->users.value(me());
}

bool TelegramQml::online() const
{
    return p->online;
}

void TelegramQml::setOnline(bool stt)
{
    if( p->online == stt )
        return;

    p->online = stt;
    if( p->telegram && p->authLoggedIn )
        p->telegram->accountUpdateStatus(!p->online || p->invisible);

    Q_EMIT onlineChanged();
}

void TelegramQml::setInvisible(bool stt)
{
    if( p->invisible == stt )
        return;

    p->invisible = stt;
    Q_EMIT invisibleChanged();

    p->telegram->accountUpdateStatus(true);
}

bool TelegramQml::invisible() const
{
    return p->invisible;
}

void TelegramQml::setAutoRewakeInterval(int ms)
{
    if(p->autoRewakeInterval == ms)
        return;
    if(p->sleepTimer)
        delete p->sleepTimer;
    if(p->wakeTimer)
        delete p->wakeTimer;

    p->sleepTimer = 0;
    p->wakeTimer = 0;
    p->autoRewakeInterval = ms;

    if(p->autoRewakeInterval)
    {
        p->wakeTimer = new QTimer(this);
        p->wakeTimer->setInterval(1000);
        p->wakeTimer->setSingleShot(true);

        p->sleepTimer = new QTimer(this);
        p->sleepTimer->setInterval(p->autoRewakeInterval);
        p->sleepTimer->setSingleShot(false);

        connect(p->sleepTimer, SIGNAL(timeout()), this, SLOT(sleep()));
        connect(p->sleepTimer, SIGNAL(timeout()), p->wakeTimer, SLOT(start()));
        connect(p->wakeTimer, SIGNAL(timeout()), this, SLOT(wake()));

        p->sleepTimer->start();
    }

    Q_EMIT autoRewakeIntervalChanged();
}

int TelegramQml::autoRewakeInterval() const
{
    return p->autoRewakeInterval;
}

int TelegramQml::unreadCount() const
{
    return p->unreadCount;
}

qreal TelegramQml::totalUploadedPercent() const
{
    return p->totalUploadedPercent;
}

bool TelegramQml::authNeeded() const
{
    return p->authNeeded;
}

bool TelegramQml::authLoggedIn() const
{
    return p->authLoggedIn;
}

bool TelegramQml::authPhoneChecked() const
{
    return p->phoneChecked;
}

bool TelegramQml::authPhoneRegistered() const
{
    return p->phoneRegistered;
}

bool TelegramQml::authPhoneInvited() const
{
    return p->phoneInvited;
}

bool TelegramQml::connected() const
{
    if( !p->telegram )
        return false;

    return p->telegram->isConnected();
}

bool TelegramQml::uploadingProfilePhoto() const
{
    return p->profile_upload_id != 0;
}

QString TelegramQml::authSignUpError() const
{
    return p->authSignUpError;
}

QString TelegramQml::authSignInError() const
{
    return p->authSignInError;
}

QString TelegramQml::error() const
{
    return p->error;
}

void TelegramQml::setLogLevel(int level)
{
    switch(level)
    {
    case LogLevelClean:
        QLoggingCategory::setFilterRules("tg.*=false");
        break;

    case LogLevelUseful:
        QLoggingCategory::setFilterRules("tg.core.settings=false\n"
                                         "tg.core.outboundpkt=false\n"
                                         "tg.core.inboundpkt=false");
        break;

    case LogLevelFull:
        QLoggingCategory::setFilterRules(QString());
        break;
    }
}

void TelegramQml::authCheckPhone(const QString &phone)
{
    p->checkphone_req_id = 0;
    qint64 id = p->telegram->authCheckPhone(phone);
    p->phoneCheckIds.insert(id, phone);
}

// WARNING: Push notifications supported for one account only!
void TelegramQml::accountRegisterDevice(const QString &token, const QString &appVersion) {
    p->userdata->setPushToken(token);
    p->telegram->accountRegisterDevice(token, appVersion);
}

void TelegramQml::accountUnregisterDevice(const QString &token) {
    p->telegram->accountUnregisterDevice(token);
}

void TelegramQml::mute(qint64 peerId) {
    if(p->userdata) {
        p->userdata->addMute(peerId);
    }
    if(!p->globalMute)
        return;

    //Mute until (time from now during which the contact is muted in seconds): set to 50 years to reach a rather permanent mute :P
    accountUpdateNotifySettings(peerId, 1576800000);
}

void TelegramQml::unmute(qint64 peerId) {
    if(p->userdata) {
        p->userdata->removeMute(peerId);
    }
    if(!p->globalMute)
        return;

    //Mute until (time from now during which the contact is muted in seconds): set to 0 to unmute
    accountUpdateNotifySettings(peerId, 0);
}

void TelegramQml::accountUpdateNotifySettings(qint64 peerId, qint32 muteUntil) {
    InputPeer::InputPeerClassType inputPeerType = getInputPeerType(peerId);
    InputPeer peer(inputPeerType);
    if(inputPeerType == InputPeer::typeInputPeerChat)
        peer.setChatId(peerId);
    else if(inputPeerType == InputPeer::typeInputPeerChannel)
        peer.setChannelId(peerId);
    else
        peer.setUserId(peerId);
    UserObject *user = p->users.value(peerId);
    if(user && user->accessHash())
        peer.setAccessHash(user->accessHash());

    InputNotifyPeer inputNotifyPeer(InputNotifyPeer::typeInputNotifyPeer);
    inputNotifyPeer.setPeer(peer);

    InputPeerNotifySettings settings;
    settings.setMuteUntil(muteUntil);

    p->telegram->accountUpdateNotifySettings(inputNotifyPeer, settings);
}

void TelegramQml::setGlobalMute(bool stt)
{
    if(p->globalMute == stt)
        return;

    p->globalMute = stt;
    Q_EMIT globalMuteChanged();
}

bool TelegramQml::globalMute() const
{
    return p->globalMute;
}

void TelegramQml::helpGetInviteText(const QString &langCode)
{
    p->telegram->helpGetInviteText(langCode);
}

DialogObject *TelegramQml::dialog(qint64 id) const
{
    DialogObject *res = p->dialogs.value(id);
    if( !res )
    {
        //qWarning() << "Did not find dialog id: " << id;
        res = p->nullDialog;
    }
    return res;
}

MessageObject *TelegramQml::message(qint64 id) const
{
    MessageObject *res = p->messages.value(id);
    if( !res )
    {
        //qWarning() << "Did not find message id: " << id;
        res = p->nullMessage;
    }
    return res;
}

MessageObject *TelegramQml::message(qint32 id, qint32 peerId) const
{
    MessageObject *res = p->messages.value(QmlUtils::getUnifiedMessageKey(id, peerId));
    if( !res )
    {
        //qWarning() << "Did not find message id: " << id << ", peer id: " << peerId;
        res = p->nullMessage;
    }
    return res;
}

ChatObject *TelegramQml::chat(qint64 id) const
{
    ChatObject *res = p->chats.value(id);
    if( !res )
    {
        //qWarning() << "Did not find chat id: " << id;
        res = p->nullChat;
    }
    return res;
}

UserObject *TelegramQml::user(qint64 id) const
{
    UserObject *res = p->users.value(id);
    if( !res )
    {
        qWarning() << "Did not find user id: " << id;
        res = p->nullUser;
    }
    return res;
}

qint64 TelegramQml::messageDialogId(qint64 id) const
{
    MessageObject *msg = p->messages.value(id);
    if(!msg)
        return 0;

    qint64 dId = msg->toId()->chatId();
    if(dId == 0)
        dId = msg->toId()->channelId();
    if(dId == 0)
        dId = msg->out()? msg->toId()->userId() : msg->fromId();

    return dId;
}

DialogObject *TelegramQml::messageDialog(qint64 id) const
{
    qint64 dId = messageDialogId(id);
    DialogObject *dlg = p->dialogs.value(dId);
    if(!dlg)
        dlg = p->nullDialog;

    return dlg;
}

WallPaperObject *TelegramQml::wallpaper(qint64 id) const
{
    WallPaperObject *res = p->wallpapers_map.value(id);
    if( !res )
        res = p->nullWallpaper;
    return res;
}

MessageObject *TelegramQml::upload(qint64 id) const
{
    MessageObject *res = p->uploads.value(id);
    if( !res )
        res = p->nullMessage;
    return res;
}

ChatFullObject *TelegramQml::chatFull(qint64 id) const
{
    ChatFullObject *res = p->chatfulls.value(id);
    if( !res )
        res = p->nullChatFull;
    return res;
}

ContactObject *TelegramQml::contact(qint64 id) const
{
    ContactObject *res = p->contacts.value(id);
    if( !res )
        res = p->nullContact;
    return res;
}

EncryptedChatObject *TelegramQml::encryptedChat(qint64 id) const
{
    EncryptedChatObject *res = p->encchats.value(id);
    if( !res )
        res = p->nullEncryptedChat;
    return res;
}

DocumentObject *TelegramQml::sticker(qint64 id) const
{
    DocumentObject *res = p->documents.value(id);
    if( !res )
        res = p->nullSticker;

    return res;
}

StickerSetObject *TelegramQml::stickerSet(qint64 id) const
{
    StickerSetObject *res = p->stickerSets.value(id);
    if( !res )
        res = p->nullStickerSet;

    return res;
}

StickerSetObject *TelegramQml::stickerSetByShortName(const QString &shortName) const
{
    QHashIterator<qint64, StickerSetObject*> i(p->stickerSets);
    while(i.hasNext())
    {
        i.next();
        if(i.value()->shortName() == shortName)
            return i.value();
    }
    return p->nullStickerSet;
}

StickerPackObject *TelegramQml::stickerPack(const QString &id) const
{
    StickerPackObject *res = p->stickerPacks.value(id);
    if( !res )
        res = p->nullStickerPack;

    return res;
}

FileLocationObject *TelegramQml::locationOf(qint64 id, qint64 dcId, qint64 accessHash, QObject *parent)
{
    FileLocationObject *obj = p->accessHashes.value(accessHash);
    if( obj && TqObject::isValid(obj) )
        return obj;

    FileLocation location(FileLocation::typeFileLocation);
    obj = new FileLocationObject(location,parent);
    obj->setId(id);
    obj->setDcId(dcId);
    obj->setAccessHash(accessHash);

    connect(obj, SIGNAL(destroyed(QObject*)), SLOT(objectDestroyed(QObject*)));

    p->accessHashes[accessHash] = obj;
    return obj;
}

FileLocationObject *TelegramQml::locationOfPhoto(PhotoObject *photo)
{
    PhotoSizeList *list = photo->sizes();
    QObject *parent = photo;
    if(list->count())
    {
        int maxIdx = 0,
            maxSize = 0;

        for(int i=0; i<list->count(); i++)
        {
            PhotoSizeObject *size = list->at(i);
            if(maxSize == 0)
                maxSize = size->w();
            else
            if(size->w() >= maxSize)
            {
                maxIdx = i;
                maxSize = size->w();
            }
        }

        FileLocationObject *location = list->at(maxIdx)->location();
        if(location->volumeId())
            return location;

        parent = list->at(maxIdx);
    }

    return locationOf(photo->id(), 0, photo->accessHash(), parent);
}

FileLocationObject *TelegramQml::locationOfThumbPhoto(PhotoObject *photo)
{
    PhotoSizeList *list = photo->sizes();
    if(list->count())
    {
        int minIdx = 0,
            minSize = 0;

        for(int i=0; i<list->count(); i++)
        {
            PhotoSizeObject *size = list->at(i);
            if(minSize == 0)
                minSize = size->w();
            else
            if(size->w() <= minSize)
            {
                minIdx = i;
                minSize = size->w();
            }
        }

        FileLocationObject *location = list->at(minIdx)->location();
        if(location->volumeId())
            return location;
    }

    return 0;
}

FileLocationObject *TelegramQml::locationOfDocument(DocumentObject *doc)
{
    FileLocationObject *res = locationOf(doc->id(), doc->dcId(), doc->accessHash(), doc);
    res->setMimeType(doc->mimeType());

    QList<DocumentAttribute> attrs = doc->attributes();
    for(int i=0; i<attrs.length(); i++)
        if(attrs.at(i).classType() == DocumentAttribute::typeDocumentAttributeFilename)
            res->setFileName(attrs.at(i).fileName());

    return res;
}

FileLocationObject *TelegramQml::locationOfVideo(VideoObject *vid)
{
    return locationOf(vid->id(), vid->dcId(), vid->accessHash(), vid);
}

FileLocationObject *TelegramQml::locationOfAudio(AudioObject *aud)
{
    return locationOf(aud->id(), aud->dcId(), aud->accessHash(), aud);
}

bool TelegramQml::documentIsSticker(DocumentObject *doc)
{
    if(!doc)
        return false;

    QList<DocumentAttribute> attrs = doc->attributes();
    Q_FOREACH(DocumentAttribute attr, attrs)
        if(attr.classType() == DocumentAttribute::typeDocumentAttributeSticker)
            return true;

    return false;
}

qint64 TelegramQml::documentStickerId(DocumentObject *doc)
{
    if(!doc)
        return 0;

    QList<DocumentAttribute> attrs = doc->attributes();
    Q_FOREACH(DocumentAttribute attr, attrs)
        if(attr.classType() == DocumentAttribute::typeDocumentAttributeSticker)
            return attr.stickerset().id();

    return 0;
}

QString TelegramQml::documentFileName(DocumentObject *doc)
{
    if(!doc)
        return QString();

    const QList<DocumentAttribute> &attrs = doc->attributes();
    Q_FOREACH(DocumentAttribute attr, attrs)
        if(attr.classType() == DocumentAttribute::typeDocumentAttributeFilename)
            return attr.fileName();

    return QString();
}

DialogObject *TelegramQml::fakeDialogObject(qint64 id, bool isChat)
{
    if( p->dialogs.contains(id) )
        return p->dialogs.value(id);
    if( p->fakeDialogs.contains(id) )
        return p->fakeDialogs.value(id);

    Peer peer(isChat? Peer::typePeerChat : Peer::typePeerUser);
    if( isChat )
        peer.setChatId(id);
    else
        peer.setUserId(id);

    Dialog dialog;
    dialog.setPeer(peer);

    DialogObject *obj = new DialogObject(dialog);
    p->fakeDialogs[id] = obj;
    return obj;
}

DialogObject *TelegramQml::nullDialog() const
{
    return p->nullDialog;
}

MessageObject *TelegramQml::nullMessage() const
{
    return p->nullMessage;
}

ChatObject *TelegramQml::nullChat() const
{
    return p->nullChat;
}

UserObject *TelegramQml::nullUser() const
{
    return p->nullUser;
}

WallPaperObject *TelegramQml::nullWallpaper() const
{
    return p->nullWallpaper;
}

UploadObject *TelegramQml::nullUpload() const
{
    return p->nullUpload;
}

ChatFullObject *TelegramQml::nullChatFull() const
{
    return p->nullChatFull;
}

ContactObject *TelegramQml::nullContact() const
{
    return p->nullContact;
}

FileLocationObject *TelegramQml::nullLocation() const
{
    return p->nullLocation;
}

EncryptedChatObject *TelegramQml::nullEncryptedChat() const
{
    return p->nullEncryptedChat;
}

EncryptedMessageObject *TelegramQml::nullEncryptedMessage() const
{
    return p->nullEncryptedMessage;
}

DocumentObject *TelegramQml::nullSticker() const
{
    return p->nullSticker;
}

StickerSetObject *TelegramQml::nullStickerSet() const
{
    return p->nullStickerSet;
}

StickerPackObject *TelegramQml::nullStickerPack() const
{
    return p->nullStickerPack;
}

QString TelegramQml::fileLocation(FileLocationObject *l)
{
    QObject *obj = l;
    qint64 dId = 0;
    bool isSticker = false;
    bool profilePic = false;
    bool thumbPic = false;
    QString realFileName;
    while(obj)
    {
        if(qobject_cast<ChatObject*>(obj))
        {
            dId = static_cast<ChatObject*>(obj)->id();
            profilePic = true;
            break;
        }
        else
        if(qobject_cast<UserObject*>(obj))
        {
            dId = static_cast<UserObject*>(obj)->id();
            profilePic = true;
            break;
        }
        else
        if(qobject_cast<MessageObject*>(obj))
        {
            dId = messageDialogId( static_cast<MessageObject*>(obj)->id() );
            break;
        }

        if(qobject_cast<DocumentObject*>(obj))
        {
            DocumentObject *doc = static_cast<DocumentObject*>(obj);
            isSticker = documentIsSticker(doc);
            realFileName = documentFileName(doc);
        }
        if(qobject_cast<PhotoSizeObject*>(obj))
        {
            PhotoSizeObject *psz = static_cast<PhotoSizeObject*>(obj);
            QObject *psz_parent = psz->parent();
            if(qobject_cast<PhotoSizeList*>(psz_parent))
            {
                PhotoSizeList *list = static_cast<PhotoSizeList*>(psz_parent);
                PhotoSizeObject *min_sz = list->first();
                for(int i=0; i<list->count() && min_sz; i++)
                    if(list->at(i)->w() < min_sz->w())
                        min_sz = list->at(i);

                thumbPic = (min_sz == psz && list->count()>1);
            }
            else
            if(qobject_cast<VideoObject*>(psz_parent))
                thumbPic = true;
            else
            if(qobject_cast<DocumentObject*>(psz_parent))
                thumbPic = true;
        }

        obj = obj->parent();
    }

    QString partName;
    if(isSticker)
        partName = thumbPic? "/sticker/thumb" : "/sticker";
    else
    if(thumbPic)
        partName = "/" + QString::number(dId) + "/thumb";
    else
    if(profilePic)
        partName = "/" + QString::number(dId) + "/profile";
    else
        partName = "/" + QString::number(dId);

    if(!realFileName.isEmpty())
        realFileName = realFileName.left(realFileName.lastIndexOf(".")) + "_-_";
    if(isSticker)
        realFileName.clear();

    const QString & dpath = downloadPath() + partName;
    const QString & fname = l->accessHash()!=0? QString("%1%2").arg(realFileName).arg(QString::number(l->id())) :
                                                QString("%1%2_%3").arg(realFileName).arg(l->volumeId()).arg(l->localId());

    QDir().mkpath(dpath);

    // For known file type extensions (e.g. stickers -> .webp), don't loop over the whole cache dir
    // as looping over a big cache is very slow.
    if (isSticker) {
        return dpath + "/" + fname + ".webp";
    }

    const QStringList & av_files = QDir(dpath).entryList({fname + "*"}, QDir::Files);
    Q_FOREACH( const QString & f, av_files )
        if( QFileInfo(f).baseName() == fname )
            return dpath + "/" + f;

    QString result = dpath + "/" + fname;
    const QString & old_path = fileLocation_old2(l);
    if(QFileInfo::exists(old_path))
    {
        QFileInfo file(old_path);
        result += "." + file.suffix();

        QFile::rename(old_path, result);
    }

    return result;
}

QString TelegramQml::videoThumbLocation(const QString &pt, TelegramThumbnailer_Callback callback)
{
    QString path = pt;
    if(path.left(localFilesPrePath().length()) == localFilesPrePath())
        path = path.mid(localFilesPrePath().length());
    if(path.isEmpty())
        return QString();

    const QString &thumb = path + ".jpg";
    if(QFileInfo::exists(thumb))
        return localFilesPrePath() + thumb;

    p->thumbnailer.createThumbnail(path, thumb, callback);
    return localFilesPrePath() + thumb;
}

QString TelegramQml::audioThumbLocation(const QString &pt)
{
    QString path = pt;
    if(path.left(localFilesPrePath().length()) == localFilesPrePath())
        path = path.mid(localFilesPrePath().length());
    if(path.isEmpty())
        return QString();

    const QString &thumb = path + ".jpg";
    if(QFileInfo::exists(thumb))
        return localFilesPrePath() + thumb;

    createAudioThumbnail(path, thumb);
    return localFilesPrePath() + thumb;
}

QString TelegramQml::fileLocation_old(FileLocationObject *l)
{
    const QString & dpath = downloadPath();
    const QString & fname = l->accessHash()!=0? QString::number(l->id()) :
                                                QString("%1_%2").arg(l->volumeId()).arg(l->localId());

    QDir().mkpath(dpath);

    const QStringList & av_files = QDir(dpath).entryList(QDir::Files);
    Q_FOREACH( const QString & f, av_files )
        if( QFileInfo(f).baseName() == fname )
            return dpath + "/" + f;

    return dpath + "/" + fname;
}

QString TelegramQml::fileLocation_old2(FileLocationObject *l)
{
    QObject *obj = l;
    qint64 dId = 0;
    bool isSticker = false;
    while(obj)
    {
        if(qobject_cast<ChatObject*>(obj))
        {
            dId = static_cast<ChatObject*>(obj)->id();
            break;
        }
        else
        if(qobject_cast<UserObject*>(obj))
        {
            dId = static_cast<UserObject*>(obj)->id();
            break;
        }
        else
        if(qobject_cast<MessageObject*>(obj))
        {
            dId = messageDialogId( static_cast<MessageObject*>(obj)->id() );
            break;
        }

        if(qobject_cast<DocumentObject*>(obj))
        {
            DocumentObject *doc = static_cast<DocumentObject*>(obj);
            isSticker = documentIsSticker(doc);
        }

        obj = obj->parent();
    }

    const QString & dpath = downloadPath() + "/" + QString::number(dId);
    const QString & fname = l->accessHash()!=0? QString::number(l->id()) :
                                                QString("%1_%2").arg(l->volumeId()).arg(l->localId());

    QDir().mkpath(dpath);

    const QStringList & av_files = QDir(dpath).entryList(QDir::Files);
    Q_FOREACH( const QString & f, av_files )
        if( QFileInfo(f).baseName() == fname )
            return dpath + "/" + f;

    QString result = dpath + "/" + fname;
    const QString & old_path = fileLocation_old(l);
    if(QFileInfo::exists(old_path))
    {
        QFileInfo file(old_path);
        result += "." + file.suffix();

        QFile::rename(old_path, result);
    }
    if(isSticker && result.right(5) != ".webp")
        result += ".webp";

    return result;
}

QString TelegramQml::localFilesPrePath()
{
#ifdef Q_OS_WIN
    return "file:///";
#else
    return "file://";
#endif
}

bool TelegramQml::createAudioThumbnail(const QString &audio, const QString &output)
{
#if defined(Q_OS_WIN) || defined(Q_OS_MAC)
    Q_UNUSED(audio)
    Q_UNUSED(output)
    return false;
#endif

    const QString command = "eyeD3";
    const QString coverName = "FRONT_COVER";
    const QString uuid = QUuid::createUuid().toString();
    const QString tmpDir = QDir::tempPath() + "/" + uuid;

    QDir().mkpath(tmpDir);

    QStringList args;
    args << "--write-images=" + tmpDir;
    args << audio;

    QProcess prc;
    prc.start(command, args);
    prc.waitForStarted();
    prc.waitForFinished();

    if(prc.exitCode() != 0)
    {
        removeFiles(tmpDir);
        return false;
    }

    QString file;
    const QStringList files = QDir(tmpDir).entryList(QDir::Files);
    Q_FOREACH(const QString &f, files)
        if(f.left(coverName.length()) == coverName)
        {
            file = tmpDir + "/" + f;
            break;
        }

    if(file.isEmpty())
    {
        removeFiles(tmpDir);
        return false;
    }

    QImageWriter writer(output);
    writer.write(QImage(file));

    removeFiles(tmpDir);
    return true;
}

QList<qint64> TelegramQml::dialogs() const
{
    return p->dialogs_list;
}

QList<qint64> TelegramQml::messages( qint64 did, qint64 maxId ) const
{
    QList<qint64> res = p->messages_list[did];
    for(int i=0; maxId && i<res.count(); i++)
    {
        if(res.at(i) <= maxId)
            continue;

        res.removeAt(i);
        i--;
    }

    return res;
}

QList<qint64> TelegramQml::wallpapers() const
{
    return p->wallpapers_map.keys();
}

QList<qint64> TelegramQml::uploads() const
{
    return p->uploads.keys();
}

QList<qint64> TelegramQml::contacts() const
{
    return p->contacts.keys();
}

QList<qint64> TelegramQml::stickers() const
{
    return p->stickers.toList();
}

QList<qint64> TelegramQml::installedStickerSets() const
{
    return p->installedStickerSets.toList();
}

QList<qint64> TelegramQml::stickerSets() const
{
    return p->stickerSets.keys();
}

QList<qint64> TelegramQml::stickerSetDocuments(qint64 id) const
{
    return p->stickersMap.value(id).toList();
}

QList<qint64> TelegramQml::stickerSetDocuments(const QString &shortName) const
{
    const qint64 id = p->stickerShortIds.value(shortName);
    if(!id)
        return QList<qint64>();

    return stickerSetDocuments(id);
}

InputUser TelegramQml::getInputUser(qint64 userId) const
{
    UserObject *user = p->users.value(userId);
    InputUser::InputUserClassType inputUserType = InputUser::typeInputUserEmpty;
    InputUser inputUser;
    if (!user)
        return inputUser;

    switch (user->classType())
    {
    case User::typeUser:
        inputUserType = InputUser::typeInputUser;
        break;
    }

    inputUser.setUserId(userId);
    inputUser.setClassType(inputUserType);
    if (user->accessHash()) {
        inputUser.setAccessHash(user->accessHash());
    }
    return inputUser;
}

InputPeer TelegramQml::getInputPeer(qint64 peerId)
{
    InputPeer::InputPeerClassType inputPeerType = getInputPeerType(peerId);
    InputPeer peer(inputPeerType);
    if(inputPeerType == InputPeer::typeInputPeerChat)
        peer.setChatId(peerId);
    else if(inputPeerType == InputPeer::typeInputPeerChannel)
    {
        peer.setChannelId(peerId);
        ChatObject *chat = p->chats.value(peerId);
        if(chat)
            peer.setAccessHash(chat->accessHash());
    }
    else
        peer.setUserId(peerId);
    UserObject *user = p->users.value(peerId);
    if(user)
        peer.setAccessHash(user->accessHash());

    return peer;
}

QList<qint64> TelegramQml::userIndex(const QString &kw)
{
    const QString & keyword = kw.toLower();

    QList<qint64> result;
    QSet<qint64> addeds;

    QMapIterator<QString, qint64> i(p->userNameIndexes);
    while(i.hasNext())
    {
        i.next();
        qint64 uid = i.value();
        if(addeds.contains(uid))
            continue;
        else
        if(!i.key().contains(keyword.toLower()))
            continue;

        result << uid;
        addeds.insert(uid);
    }

    return result;
}

void TelegramQml::authLogout()
{
    if( !p->telegram )
        return;
    if( p->logout_req_id )
        return;

    QString token = p->userdata->pushToken();
    if (!token.isEmpty()) {
        p->loggingOut = true;
        p->telegram->accountUnregisterDevice(token);
    } else {
        p->logout_req_id = p->telegram->authLogOut();
    }
}

void TelegramQml::authResetAuthorizations()
{
    if (!p->telegram)
        return;

    p->telegram->authResetAuthorizations();
}

void TelegramQml::authSendCall()
{
    if( !p->telegram )
        return;

    p->telegram->authSendCall();
}

void TelegramQml::authSendCode()
{
    if( !p->telegram )
        return;

    p->telegram->authSendCode();
}

void TelegramQml::authSendInvites(const QStringList &phoneNumbers, const QString &inviteText)
{
    if( !p->telegram )
        return;

    p->telegram->authSendInvites(phoneNumbers, inviteText);
}

void TelegramQml::authSignIn(const QString &code, bool retry)
{
    if( !p->telegram )
        return;

    if(!retry)
        p->authCheckPhoneRetry = 0;

    p->authSignInCode = code;
    p->telegram->authSignIn(p->authSignInCode);

    p->authNeeded = false;
    p->authSignUpError.clear();
    p->authSignInError.clear();
    Q_EMIT authSignInErrorChanged();
    Q_EMIT authSignUpErrorChanged();
    Q_EMIT authNeededChanged();
}

void TelegramQml::authSignUp(const QString &code, const QString &firstName, const QString &lastName)
{
    if( !p->telegram )
        return;

    p->telegram->authSignUp(code, firstName, lastName);

    p->authNeeded = false;
    p->authSignUpError.clear();
    p->authSignInError.clear();
    Q_EMIT authSignInErrorChanged();
    Q_EMIT authSignUpErrorChanged();
    Q_EMIT authNeededChanged();
}

void TelegramQml::authCheckPassword(const QString &pass)
{
    if( !p->telegram )
        return;

    QByteArray salt = QByteArray::fromHex(p->currentSalt.toUtf8());
    QByteArray passData = salt + pass.toUtf8() + salt;
    p->telegram->authCheckPassword(QCryptographicHash::hash(passData, QCryptographicHash::Sha256));
}

void TelegramQml::accountUpdateProfile(const QString &firstName, const QString &lastName)
{
    if (!p->telegram)
        return;

    p->telegram->accountUpdateProfile(firstName, lastName);
}

void TelegramQml::usersGetFullUser(qint64 userId)
{
    if (!p->telegram)
        return;

    p->telegram->usersGetFullUser(getInputUser(userId));
}

void TelegramQml::accountCheckUsername(const QString &username)
{
    if (!p->telegram) return;
    p->telegram->accountCheckUsername(username);
}

void TelegramQml::accountUpdateUsername(const QString &username)
{
    if (!p->telegram) return;
    p->telegram->accountUpdateUsername(username);
}

void TelegramQml::contactsBlock(qint64 userId)
{
    InputUser inputUser = getInputUser(userId);
    qint64 requestId = p->telegram->contactsBlock(inputUser);
    p->blockRequests.insert(requestId, userId);
}

void TelegramQml::contactsUnblock(qint64 userId)
{
    InputUser inputUser = getInputUser(userId);
    qint64 requestId = p->telegram->contactsUnblock(inputUser);
    p->unblockRequests.insert(requestId, userId);
}

qint32 TelegramQml::sendMessage(qint64 dId, const QString &msg, qint32 replyTo)
{
    if( !p->telegram )
        return 0;

    DialogObject *dlg = p->dialogs.value(dId);
    InputPeer peer = getInputPeer(dId);

    qint64 sendId;
    Message message = newMessage(dId);
    message.setMessage(msg);
    message.setReplyToMsgId(replyTo);

    p->msg_send_random_id = generateRandomId();
    if(dlg && dlg->encrypted())
    {
        sendId = p->telegram->messagesSendEncrypted(dId, p->msg_send_random_id, 0, msg);
    }
    else
    {

        auto entities = QList<MessageEntity>();
        sendId = p->telegram->messagesSendMessage(true, false, peer, replyTo, msg, p->msg_send_random_id, ReplyMarkup(), entities);
    }

    insertMessage(message, (dlg && dlg->encrypted()), false, true);
    auto unifiedId = QmlUtils::getUnifiedMessageKey(message.id(), message.toId().channelId());
    MessageObject *msgObj = p->messages.value(unifiedId);
    msgObj->setSent(false);

    p->pend_messages[sendId] = msgObj;

    timerUpdateDialogs();
    return sendId;
}

bool TelegramQml::sendMessageAsDocument(qint64 dId, const QString &msg)
{
    QDir().mkpath(tempPath());
    const QString &path = tempPath() + "/message-text.txt";

    QFile::remove(path);
    QFile file(path);
    if(!file.open(QFile::WriteOnly))
        return false;

    file.write(msg.toUtf8());
    file.close();

    return sendFile(dId, path, true);
}

void TelegramQml::forwardDocument(qint64 dialogId, DocumentObject *doc)
{
    if(!p->telegram)
        return;

    InputPeer peer = getInputPeer(dialogId);
    p->msg_send_random_id = generateRandomId();
    p->telegram->messagesForwardDocument(peer, p->msg_send_random_id, doc->id(), doc->accessHash() );
}

void TelegramQml::addContact(const QString &firstName, const QString &lastName, const QString &phoneNumber)
{
    InputContact contact;
    contact.setFirstName(firstName);
    contact.setLastName(lastName);
    contact.setPhone(phoneNumber);

    p->telegram->contactsImportContacts(QList<InputContact>() << contact, false);
}

void TelegramQml::addContacts(const QVariantList &vcontacts)
{
    QList<InputContact> contacts;
    Q_FOREACH(const QVariant &v, vcontacts)
    {
        InputContact contact;
        const QMap<QString, QVariant> map = v.toMap();
        contact.setPhone(map.value("phone").toString());
        contact.setFirstName(map.value("firstName").toString());
        contact.setLastName(map.value("lastName").toString());
        contacts << contact;
    }

    p->telegram->contactsImportContacts(contacts, false);
}

void TelegramQml::forwardMessages(QList<int> msgIds, qint64 toPeerId)
{
    const InputPeer & toPeer = getInputPeer(toPeerId);

    std::stable_sort(msgIds.begin(), msgIds.end(), qGreater<int>());

    QList<qint64> randoms;
    for(int i=0; i<msgIds.count(); i++)
        randoms << generateRandomId();

    //TODO: Resolve proper source input peer
    p->telegram->messagesForwardMessages(false, InputPeer(), msgIds, randoms, toPeer);
}

void TelegramQml::deleteMessages(QList<qint64> msgIds)
{

    if(!p->telegram)
        return;

    QList<qint32> simpleIds;

    Q_FOREACH(qint64 msgId, msgIds)
    {
        simpleIds.append(QmlUtils::getSeparateMessageId(msgId));
        MessageObject *msgObj = p->messages.value(msgId);
        if(msgObj)
        {
            p->database->deleteMessage(msgId);
            insertToGarbeges(p->messages.value(msgId));

            Q_EMIT messagesChanged(false);
        }
    }
    p->telegram->messagesDeleteMessages(simpleIds);
}

void TelegramQml::messagesCreateChat(const QList<int> &users, const QString &topic)
{
    QList<InputUser> inputUsers;
    Q_FOREACH( qint32 user, users )
    {
        InputUser input(InputUser::typeInputUser);
        input.setUserId(user);

        inputUsers << input;
    }

    p->telegram->messagesCreateChat(inputUsers, topic);
}

void TelegramQml::messagesAddChatUser(qint64 chatId, qint64 userId, qint32 fwdLimit)
{
    if(!p->telegram)
        return;

    UserObject *userObj = p->users.value(userId);
    if(!userObj)
        return;

    InputUser::InputUserClassType inputType = InputUser::typeInputUserEmpty;
    switch(userObj->classType())
    {
    case User::typeUser:
        inputType = InputUser::typeInputUser;
        break;
    }

    InputUser user(inputType);
    user.setUserId(userId);

    p->telegram->messagesAddChatUser(chatId, user, fwdLimit);
}

qint64 TelegramQml::messagesDeleteChatUser(qint64 chatId, qint64 userId)
{
    if(!p->telegram)
        return 0;

    UserObject *userObj = p->users.value(userId);
    if(!userObj)
        return 0;

    InputUser::InputUserClassType inputType = InputUser::typeInputUserEmpty;
    switch(userObj->classType())
    {
    case User::typeUser:
        inputType = InputUser::typeInputUser;
        break;
    }

    InputUser user(inputType);
    user.setUserId(userId);

    return p->telegram->messagesDeleteChatUser(chatId, user);
}

void TelegramQml::messagesEditChatTitle(qint32 chatId, const QString &title)
{
    if(!p->telegram)
        return;

    p->telegram->messagesEditChatTitle(chatId, title);
}

void TelegramQml::messagesEditChatPhoto(qint32 chatId, const QString &filePath)
{
    if (!p->telegram)
        return;

    p->telegram->messagesEditChatPhoto(chatId, filePath);
}

void TelegramQml::messagesDeleteHistory(qint64 peerId, bool deleteChat, bool userRemoved)
{
    if(!p->telegram)
        return;

    if (deleteChat) {
        // Mark chat for deletion.
        p->deleteChatIds.insert(peerId);
    } else {
        // Check if chat was alerady marked for deletion.
        deleteChat = p->deleteChatIds.contains(peerId);
    }

    const InputPeer & input = getInputPeer(peerId);

    if (p->dialogs.value(peerId)->unreadCount() > 0) {
        // Mark history as read before deleting history.
        qint64 requestId = 0;
        if(input.classType() == InputPeer::typeInputPeerChannel)
        {
            requestId = channelsReadHistory(input.channelId(), input.accessHash());
        } else {
            requestId = messagesReadHistory(peerId);//, QDateTime::currentDateTime().toTime_t());
        }
        if (requestId) {
            // Require follow up after messagesReadHistory completes.
            p->delete_history_requests.insert(requestId, peerId);
        }
        return;
    }

    if (p->chats.contains(peerId) && deleteChat && !userRemoved) {
        // Leave group chat before deleting.
        if(input.classType() == InputPeer::typeInputPeerChannel)
        {
            channelsGetFullChannel(peerId);
        } else {
            messagesGetFullChat(peerId);
        }
    } else if (p->encchats.contains(peerId)) {
        if (deleteChat == false) {
            qWarning() << "WARNING: Deleting secret chat history without chat removal is not yet unsupported";
            return;
        }
        // Discard secret chat before deleting.
        // Currently we don't allow clearing secret chat history without deletion,
        // because secret chat messages re-download from server each time anyway..
        // Is there a way we can fix this in TelegramQML?
        messagesDiscardEncryptedChat(peerId);
    } else {
        qint64 requestId = p->telegram->messagesDeleteHistory(input);
        p->delete_history_requests.insert(requestId, peerId);
    }
}

void TelegramQml::messagesSetTyping(qint64 peerId, bool stt)
{
    if(!p->telegram)
        return;

    if(p->encchats.contains(peerId))
    {
        InputEncryptedChat peer;
        peer.setChatId(peerId);

//        p->telegram->messagesSetEncryptedTyping(peer, stt);
    }
    else
    {
        const InputPeer & peer = getInputPeer(peerId);
        SendMessageAction action(SendMessageAction::typeSendMessageTypingAction);
        if(!stt)
            action.setClassType(SendMessageAction::typeSendMessageCancelAction);

        p->telegram->messagesSetTyping(peer, action);
    }

}

qint64 TelegramQml::channelsReadHistory(qint32 channelId, qint64 accessHash)
{
    if(!p->telegram)
        return 0;
    if(!channelId)
        return 0;

    qint64 result;
    InputChannel channel(InputChannel::typeInputChannel);
    channel.setChannelId(channelId);
    channel.setAccessHash(accessHash);
    result = p->telegram->channelsReadHistory(channel, 0);
    p->read_history_requests.insert(result, channelId);
    return result;
}

void TelegramQml::channelsDeleteMessages(qint32 channelId, qint64 accessHash, QList<qint64> msgIds)
{
    if(!p->telegram)
        return;
    if(!channelId)
        return;

    QList<qint32> simpleIds;

    Q_FOREACH(qint64 msgId, msgIds)
    {
        simpleIds.append(QmlUtils::getSeparateMessageId(msgId));
        MessageObject *msgObj = p->messages.value(msgId);
        if(msgObj)
        {
            p->database->deleteMessage(msgId);
            insertToGarbeges(p->messages.value(msgId));

            Q_EMIT messagesChanged(false);
        }
    }
    InputChannel channel(InputChannel::typeInputChannel);
    channel.setChannelId(channelId);
    channel.setAccessHash(accessHash);
    p->telegram->channelsDeleteMessages(channel, simpleIds);

}

qint64 TelegramQml::messagesReadHistory(qint64 peerId, qint32 maxDate)
{
    if(!p->telegram)
        return 0;
    if(!peerId)
        return 0;

    qint64 result;
    const InputPeer & peer = getInputPeer(peerId);
    if(!p->encchats.contains(peerId)) {
        result = p->telegram->messagesReadHistory(peer);
    }  else {
        if (maxDate == 0) {
            maxDate = (qint32) QDateTime::currentDateTime().toTime_t();
        }
        result = p->telegram->messagesReadEncryptedHistory(peerId, maxDate);
    }

    p->read_history_requests.insert(result, peerId);
    return result;
}

void TelegramQml::messagesCreateEncryptedChat(qint64 userId)
{
    if( !p->telegram )
        return;

    UserObject *user = p->users.value(userId);
    if(!user)
        return;

    InputUser input(InputUser::typeInputUser);
    input.setUserId(user->id());
    input.setAccessHash(user->accessHash());

    p->telegram->messagesCreateEncryptedChat(input);
}

void TelegramQml::messagesAcceptEncryptedChat(qint32 chatId)
{
    if( !p->telegram )
        return;

    p->telegram->messagesAcceptEncryptedChat(chatId);
}

qint64 TelegramQml::messagesDiscardEncryptedChat(qint32 chatId, bool force)
{
    if( !p->telegram )
        return 0;

    if(force)
    {
        p->deleteChatIds.insert(chatId);
        deleteLocalHistory(chatId);
    }

    return p->telegram->messagesDiscardEncryptedChat(chatId);
}

void TelegramQml::messagesGetFullChat(qint32 chatId)
{
    if(!p->telegram)
        return;

    p->telegram->messagesGetFullChat(chatId);
}

void TelegramQml::channelsGetFullChannel(qint32 peerId)
{
    if(!p->telegram)
        return;

    const InputPeer & input = getInputPeer(peerId);
    InputChannel channel(InputChannel::typeInputChannel);
    channel.setChannelId(input.channelId());
    channel.setAccessHash(input.accessHash());
    qWarning() << "channelsGetFullChannel: Channel Id: " << input.channelId() << ", accessHash: " << input.accessHash();
    p->telegram->channelsGetFullChannel(channel);
}

void TelegramQml::installStickerSet(const QString &shortName)
{
    if(!p->telegram)
        return;

    InputStickerSet set(InputStickerSet::typeInputStickerSetShortName);
    set.setShortName(shortName);

    qint64 msgId = p->telegram->messagesInstallStickerSet(set, false);
    p->pending_stickers_install[msgId] = shortName;
}

void TelegramQml::uninstallStickerSet(const QString &shortName)
{
    if(!p->telegram)
        return;

    InputStickerSet set(InputStickerSet::typeInputStickerSetShortName);
    set.setShortName(shortName);

    qint64 msgId = p->telegram->messagesUninstallStickerSet(set);
    p->pending_stickers_uninstall[msgId] = shortName;
}

void TelegramQml::getStickerSet(const QString &shortName)
{
    if(!p->telegram)
        return;

    InputStickerSet set(InputStickerSet::typeInputStickerSetShortName);
    set.setShortName(shortName);

    p->telegram->messagesGetStickerSet(set);
}

void TelegramQml::getStickerSet(DocumentObject *doc)
{
    if(!p->telegram)
        return;
    if(!doc)
        return;

    QList<DocumentAttribute> attrs = doc->attributes();
    Q_FOREACH(DocumentAttribute attr, attrs)
        if(attr.classType() == DocumentAttribute::typeDocumentAttributeSticker)
        {
            qint64 msgId = p->telegram->messagesGetStickerSet(attr.stickerset());
            p->pending_doc_stickers[msgId] = doc;
            break;
        }
}

void TelegramQml::search(const QString &keyword)
{
    if(!p->telegram)
        return;

    InputPeer peer(InputPeer::typeInputPeerEmpty);
    MessagesFilter filter(MessagesFilter::typeInputMessagesFilterEmpty);

    p->telegram->messagesSearch(false, peer, keyword, filter, 0, 0, 0, 0, 50);
}

void TelegramQml::searchContact(const QString &keyword)
{
    if(!p->telegram)
        return;

    p->telegram->contactsSearch(keyword);
}

qint64 TelegramQml::sendFile(qint64 dId, const QString &fpath, bool forceDocument, bool forceAudio)
{
    QString file = fpath;
    if( file.left(localFilesPrePath().length()) == localFilesPrePath() )
        file = file.mid(localFilesPrePath().length());

    if( !QFileInfo::exists(file) )
        return 0;
    if( !p->telegram )
        return 0;

    DialogObject *dlg = dialog(dId);
    if( !dlg )
        return 0;

    Message message = newMessage(dId);
    InputPeer peer = getInputPeer(dId);

    qint64 fileId;
    p->msg_send_random_id = generateRandomId();
    const QMimeType & t = p->mime_db.mimeTypeForFile(file);

    QString thumbnail = p->thumbnailer.getThumbPath(tempPath(), fpath);
    if (t.name().contains("video/") && !p->thumbnailer.hasThumbnail(thumbnail)) {
#ifdef TG_THUMBNAILER_CPP11
        TelegramThumbnailer_Callback callBack = [this, dId, fpath, forceDocument, forceAudio]() {
            sendFile(dId, fpath, forceDocument, forceAudio);
        };
#else
        TelegramThumbnailer_Callback callBack;
        callBack.object = this;
        callBack.method = "sendFile";
        callBack.args << dId << fpath << forceDocument << forceAudio;
#endif

        p->thumbnailer.createThumbnail(fpath, thumbnail,callBack);
        // Stop here. We'll call back to this method once thumbnailer's done.
        return true;
    }

    QImageReader reader(thumbnail);
    QSize size = reader.size();
    if (size.width() == 0 || size.height() == 0) {
        thumbnail.clear();
    }

    if( (t.name().contains("webp") || fpath.right(5) == ".webp") && !dlg->encrypted() && !forceDocument && !forceAudio )
    {
        QImageReader reader(file);
        const QSize imageSize = reader.size();
        reader.setScaledSize(QSize(200, 200.0*imageSize.height()/imageSize.width()));

        QString thumbnail = tempPath() +"/cutegram_thumbnail_" + QUuid::createUuid().toString() + ".webp";
        QImageWriter writer(thumbnail);
        writer.write(reader.read());

        fileId = p->telegram->messagesSendDocument(peer, p->msg_send_random_id, file, thumbnail, true);

        MessageMedia media = message.media();
        media.setClassType(MessageMedia::typeMessageMediaDocument);

        Document document = media.document();
        document.setAttributes( document.attributes() << DocumentAttribute(DocumentAttribute::typeDocumentAttributeSticker) );

        media.setDocument(document);
        message.setMedia(media);
    }
    else
    if( !t.name().contains("gif") && t.name().contains("image/") && !forceDocument && !forceAudio )
    {
        if(dlg->encrypted())
            fileId = p->telegram->messagesSendEncryptedPhoto(dId, p->msg_send_random_id, 0, file);
        else
            fileId = p->telegram->messagesSendPhoto(peer, p->msg_send_random_id, file);

        MessageMedia media = message.media();
        media.setClassType(MessageMedia::typeMessageMediaPhoto);
        message.setMedia(media);
    }
    else
    if( t.name().contains("video/") && !forceDocument && !forceAudio )
    {
        // Video thumb already processed.

        if(dlg->encrypted())
        {
            QByteArray thumbData;
            QFile thumbFile(thumbnail);
            if(thumbFile.open(QFile::ReadOnly))
            {
                thumbData = thumbFile.readAll();
                thumbFile.close();
            }

            fileId = p->telegram->messagesSendEncryptedVideo(dId, p->msg_send_random_id, 0, file, 0, size.width(), size.height(), thumbData);
        }
        else
            fileId = p->telegram->messagesSendVideo(peer, p->msg_send_random_id, file, 0, size.width(), size.height(), thumbnail);

        MessageMedia media = message.media();
        media.setClassType(MessageMedia::typeMessageMediaVideo);
        message.setMedia(media);
    }
    else
    if( (t.name().contains("audio/") || forceAudio) && !forceDocument && !dlg->encrypted() )
    {
        fileId = p->telegram->messagesSendAudio(peer, p->msg_send_random_id, file, 0);

        MessageMedia media = message.media();
        media.setClassType(MessageMedia::typeMessageMediaAudio);
        message.setMedia(media);
    }
    else
    {
        QString thumbnail = tempPath()+"/cutegram_thumbnail_" + QUuid::createUuid().toString() + ".jpg";
        if(t.name().contains("audio/"))
        {
            if(!createAudioThumbnail(file, thumbnail))
                thumbnail.clear();
        }
        else
        if(t.name().contains("video/"))
        {
            // Video thumb already processed.
        }
        else
            thumbnail.clear();

        if(dlg->encrypted())
            fileId = p->telegram->messagesSendEncryptedDocument(dId, p->msg_send_random_id, 0, file);
        else
            fileId = p->telegram->messagesSendDocument(peer, p->msg_send_random_id, file, thumbnail);

        MessageMedia media = message.media();
        media.setClassType(MessageMedia::typeMessageMediaDocument);
        message.setMedia(media);
    }

    insertMessage(message, false, false, true);

    auto unifiedId = QmlUtils::getUnifiedMessageKey(message.id(), message.toId().channelId());
    MessageObject *msgObj = p->messages.value(unifiedId);
    msgObj->setSent(false);

    UploadObject *upload = msgObj->upload();
    upload->setFileId(fileId);
    upload->setLocation(file);
    upload->setTotalSize(QFileInfo(file).size());

    connect(upload, SIGNAL(uploadedChanged())  , SLOT(refreshTotalUploadedPercent()));
    connect(upload, SIGNAL(destroyed(QObject*)), SLOT(objectDestroyed(QObject*))    );

    p->uploads[fileId] = msgObj;
    p->uploadPercents.insert(upload);

    Q_EMIT uploadsChanged();
    return fileId;
}

void TelegramQml::getFile(FileLocationObject *l, qint64 type, qint32 fileSize)
{
    if(!l)
        return;
    if( !p->telegram )
        return;
    if(l->accessHash()==0 && l->volumeId()==0 && l->localId()==0)
        return;
    if(l->download()->fileId() != 0)
        return;

    const QString & download_file = fileLocation(l);
    if( QFile::exists(download_file) )
    {
        l->download()->setLocation(FILES_PRE_STR+download_file);
        return;
    }

    InputFileLocation input(static_cast<InputFileLocation::InputFileLocationClassType>(type));
    input.setAccessHash(l->accessHash());
    input.setId(l->id());
    input.setLocalId(l->localId());
    input.setSecret(l->secret());
    input.setVolumeId(l->volumeId());

    QByteArray ekey;
    QByteArray eiv;

    QObject *parentObj = l->parent();
    MessageMediaObject *media = 0;
    while(parentObj)
    {
        parentObj = parentObj->parent();
        media = qobject_cast<MessageMediaObject*>(parentObj);
        if(media)
            break;
    }

    if(media && !media->encryptKey().isEmpty())
    {
        ekey = media->encryptKey();
        eiv  = media->encryptIv();
        input.setClassType(InputFileLocation::typeInputEncryptedFileLocation);
    }

    parentObj = l->parent();
    if(parentObj && qobject_cast<DocumentObject*>(parentObj))
    {
        DocumentObject *doc = static_cast<DocumentObject*>(parentObj);
        l->download()->setTotal(doc->size());
    }
    else
    if(parentObj && qobject_cast<PhotoSizeObject*>(parentObj))
    {
        PhotoSizeObject *psz = static_cast<PhotoSizeObject*>(parentObj);
        l->download()->setTotal(psz->size());
    }
    else
    if(parentObj && qobject_cast<AudioObject*>(parentObj))
    {
        AudioObject *aud = static_cast<AudioObject*>(parentObj);
        l->download()->setTotal(aud->size());
    }
    else
    if(parentObj && qobject_cast<VideoObject*>(parentObj))
    {
        VideoObject *vid = static_cast<VideoObject*>(parentObj);
        l->download()->setTotal(vid->size());
    }
    else
    if(parentObj && qobject_cast<UserProfilePhotoObject*>(parentObj))
    {
        UserProfilePhotoObject *upp = static_cast<UserProfilePhotoObject*>(parentObj);
        Q_UNUSED(upp)
    }
    else
        qDebug() << __FUNCTION__ << ": Can't detect size of: " << parentObj;

    qint64 fileId = p->telegram->uploadGetFile(input, fileSize, l->dcId(), ekey, eiv);
    p->downloads[fileId] = l;

    l->download()->setFileId(fileId);
}

void TelegramQml::getFileJustCheck(FileLocationObject *l)
{
    if( !p->telegram )
        return;

    const QString & download_file = fileLocation(l);
    if( QFile::exists(download_file) && !l->download()->file()->isOpen() )
    {
        l->download()->setLocation(FILES_PRE_STR+download_file);
        l->download()->setDownloaded(true);
    }
}

void TelegramQml::cancelDownload(DownloadObject *download)
{
    cancelSendGet(download->fileId());
}

Message TelegramQml::newMessage(qint64 dId)
{
    Peer::PeerClassType peerType = getPeerType(dId);

    Peer to_peer(peerType);
    to_peer.setChatId(peerType==Peer::typePeerChat?dId:0);
    to_peer.setUserId(peerType==Peer::typePeerUser?dId:0);
    to_peer.setChannelId(peerType==Peer::typePeerChannel?dId:0);

    DialogObject *dlg = dialog(dId);
    if(dlg && dlg->encrypted())
    {
        Peer encPeer(Peer::typePeerChat);
        encPeer.setChatId(dId);
        to_peer = encPeer;
    }

//    static qint32 msgId = INT_MAX-100000;
//    msgId++;

    Message message(Message::typeMessage);
    message.setId(generateRandomId());
    message.setDate( QDateTime::currentDateTime().toTime_t() );
    message.setFromId( p->telegram->ourId() );
    message.setToId(to_peer);
    message.setFlags( 0x1 | 0x2 );

    return message;
}

SecretChat *TelegramQml::getSecretChat(qint64 chatId)
{
    const QList<SecretChat*> & chats = p->tsettings->secretChats();
    Q_FOREACH(SecretChat *c, chats)
        if(c->chatId() == chatId)
            return c;

    return  0;
}

void TelegramQml::cancelSendGet(qint64 fileId)
{
    if( !p->telegram )
        return;

    if(p->downloads.contains(fileId))
        p->downloads.value(fileId)->download()->setFileId(0);

    p->telegram->uploadCancelFile(fileId);
}

void TelegramQml::setProfilePhoto(const QString &fileName)
{
    if( !p->telegram )
        return;
    if( p->profile_upload_id )
        return;

    QFileInfo file(fileName);
    QImageReader reader(fileName);
    QSize size = reader.size();
    qreal ratio = (qreal)size.width()/size.height();
    if( size.width()>1024 && size.width()>size.height() )
    {
        size.setWidth(1024);
        size.setHeight(1024/ratio);
    }
    else
    if( size.height()>1024 && size.height()>size.width() )
    {
        size.setHeight(1024);
        size.setWidth(1024*ratio);
    }

    reader.setScaledSize(size);
    const QImage & img = reader.read();

    QByteArray data;
    QBuffer buffer(&data);
    buffer.open(QBuffer::WriteOnly);

    QImageWriter writer(&buffer, "png");
    writer.write(img);

    buffer.close();

    p->profile_upload_id = p->telegram->photosUploadProfilePhoto(data, file.fileName() );
    p->upload_photo_path = fileName;

    Q_EMIT uploadingProfilePhotoChanged();
}

void TelegramQml::timerUpdateDialogs(qint32 duration)
{
    if( p->upd_dialogs_timer )
        killTimer(p->upd_dialogs_timer);

    p->upd_dialogs_timer = startTimer(duration);
}

void TelegramQml::timerUpdateContacts(qint32 duration) {
    if (p->update_contacts_timer)
        killTimer(p->update_contacts_timer);

    p->update_contacts_timer = startTimer(duration);
}

void TelegramQml::cleanUpMessages()
{
    if( !p->autoCleanUpMessages && p->messagesModels.contains(static_cast<TelegramMessagesModel*>(sender())) )
        return;

    p->cleanUpTimer->stop();
    p->cleanUpTimer->start();
}

void TelegramQml::updatesGetState()
{
    if(!p->telegram)
        return;

    if (!p->telegram->isConnected())
        return;

    p->telegram->updatesGetState();
}

void TelegramQml::updatesGetDifference()
{
    if(!p->telegram)
        return;

    p->telegram->updatesGetDifference(p->state.pts(), p->state.date(), p->state.qts());
}

void TelegramQml::updatesGetChannelDifference(qint32 channelId, qint64 accessHash)
{
    if(!p->telegram)
        return;
    if(!channelId)
        return;

    InputChannel channel(InputChannel::typeInputChannel);
    ChannelMessagesFilter filter = ChannelMessagesFilter();
    channel.setChannelId(channelId);
    channel.setAccessHash(accessHash);
    p->telegram->updatesGetChannelDifference(channel, filter, p->state.pts(), 500);
}

bool TelegramQml::sleep()
{
    if(!p->telegram)
        return false;

    return p->telegram->sleep();
}

bool TelegramQml::wake()
{
    if(!p->telegram)
        return false;

    return p->telegram->wake();
}

void TelegramQml::cleanUpMessages_prv()
{
    QSet<qint64> lockedMessages;

    /*! Find messages shouldn't be delete !*/
    Q_FOREACH(DialogObject *dlg, p->dialogs)
    {
        qint64 msgId = QmlUtils::getUnifiedMessageKey(dlg->topMessage(), dlg->peer()->channelId());
        if(msgId)
            lockedMessages.insert(msgId);
    }

    Q_FOREACH(TelegramSearchModel *mdl, p->searchModels)
    {
        const QList<qint64> &list = mdl->messages();
        Q_FOREACH(qint64 mId, list)
            lockedMessages.insert(mId);
    }

    Q_FOREACH(TelegramMessagesModel *mdl, p->messagesModels)
    {
        DialogObject *dlg = mdl->dialog();
        if(!dlg)
            continue;

        qint64 dId = dlg->peer()->userId();
        if(!dId)
            dId = dlg->peer()->chatId();
        if(!dId)
            dId = dlg->peer()->channelId();
        if(!dId)
            continue;

        const QList<qint64> &list = p->messages_list.value(dId);
        Q_FOREACH(qint64 mId, list)
            lockedMessages.insert(mId);
    }

    Q_FOREACH(qint64 msg, p->pend_messages.keys())
        lockedMessages.insert(msg);
    Q_FOREACH(qint64 msg, p->uploads.keys())
        lockedMessages.insert(msg);
    Q_FOREACH(FileLocationObject *obj, p->downloads)
    {
        QObject *parent = obj;
        while(parent && TqObject::isValid(tqobject_cast(parent)) && !qobject_cast<MessageObject*>(parent))
            parent = parent->parent();
        if(!parent)
            continue;

        lockedMessages.insert(static_cast<MessageObject*>(parent)->id());
    }
    Q_FOREACH(FileLocationObject *obj, p->accessHashes)
    {
        QObject *parent = obj;
        while(parent && TqObject::isValid(tqobject_cast(parent)) && !qobject_cast<MessageObject*>(parent))
            parent = parent->parent();
        if(!parent)
            continue;

        lockedMessages.insert(static_cast<MessageObject*>(parent)->id());
    }

    /*! Delete expired messages !*/
    QHashIterator<qint64, QList<qint64> > mli(p->messages_list);
    while(mli.hasNext())
    {
        mli.next();
        QList<qint64> messages = mli.value();
        for(int i=0; i<messages.count(); i++)
        {
            qint64 msgId = messages.at(i);
            if(lockedMessages.contains(msgId))
                continue;

            messages.removeAt(i);
            i--;
        }

        p->messages_list[mli.key()] = messages;
    }

    Q_FOREACH(MessageObject *msg, p->messages)
        if(!lockedMessages.contains(msg->unifiedId()))
        {
            p->messages.remove(msg->unifiedId());
            msg->deleteLater();
        }

    Q_EMIT dialogsChanged(false);
    Q_EMIT messagesChanged(false);
}

bool TelegramQml::requestReadMessage(qint32 msgId)
{
    getMessagesLock.lock();
    QList<qint32> singleRequest;
    singleRequest << msgId;
    p->telegram->messagesGetMessages(singleRequest);
    getMessagesLock.unlock();

//    p->messageRequester->stop();
//    p->messageRequester->start();
    return true;
}

bool TelegramQml::requestReadChannelMessage(qint32 msgId, qint32 channelId, qint64 accessHash)
{
    getMessagesLock.lock();
    QList<qint32> singleRequest;
    singleRequest << msgId;
    InputChannel channel(InputChannel::typeInputChannel);
    channel.setChannelId(channelId);
    channel.setAccessHash(accessHash);
    p->telegram->channelsGetMessages(channel, singleRequest);
    getMessagesLock.unlock();
    return true;
}

//void TelegramQml::requestReadMessage_prv()
//{
//    if(!p->telegram)
//        return;
//    getMessagesLock.lock();
//    if(p->request_messages.isEmpty())
//    {
//        getMessagesLock.unlock();
//        return;
//    }

//    }
//    p->request_messages.clear();
//    getMessagesLock.unlock();
//}

void TelegramQml::removeFiles(const QString &dir)
{
    const QStringList dirs = QDir(dir).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    Q_FOREACH(const QString &d, dirs)
        removeFiles(dir + "/" + d);

    const QStringList files = QDir(dir).entryList(QDir::Files);
    Q_FOREACH(const QString &f, files)
        QFile::remove(dir + "/" + f);

    QDir().rmdir(dir);
}

void TelegramQml::try_init()
{


    if( p->telegram )
    {
        delete p->telegram;
        p->telegram = 0;
    }
    if( p->phoneNumber.isEmpty() || p->publicKeyFile.isEmpty() || p->configPath.isEmpty() )
        return;
    if(p->defaultHostAddress.isEmpty() || !p->defaultHostPort || !p->defaultHostDcId ||
       !p->appId || p->appHash.isEmpty() )
        return;

    removeFiles(tempPath());
    QDir().mkpath(tempPath());

    QString pKeyFile = publicKeyPath();
    if(pKeyFile.left(localFilesPrePath().length()) == localFilesPrePath())
        pKeyFile = pKeyFile.mid(localFilesPrePath().length());

    p->telegram = new Telegram(p->defaultHostAddress,p->defaultHostPort,p->defaultHostDcId,
                               p->appId, p->appHash, p->phoneNumber, p->configPath, pKeyFile);

    ASSERT(connect( p->telegram, &Telegram::authNeeded, this, &TelegramQml::authNeeded_slt));
    ASSERT(connect( p->telegram, &Telegram::authLoggedIn, this, &TelegramQml::authLoggedIn_slt));
    ASSERT(connect( p->telegram, &Telegram::authLogOutAnswer, this, &TelegramQml::authLogOut_slt));
    ASSERT(connect( p->telegram, &Telegram::authCheckPhoneAnswer, this, &TelegramQml::authCheckPhone_slt));
    ASSERT(connect( p->telegram, &Telegram::authCheckPhoneError, this, &TelegramQml::authCheckPhoneError_slt));
    ASSERT(connect( p->telegram, &Telegram::authSendCallAnswer, this, &TelegramQml::authSendCall_slt));
    ASSERT(connect( p->telegram, &Telegram::authSendCodeAnswer, this, &TelegramQml::authSendCode_slt));
    ASSERT(connect( p->telegram, &Telegram::authSendCodeError, this, &TelegramQml::authSendCodeError_slt));
    ASSERT(connect( p->telegram, &Telegram::authSendInvitesAnswer, this, &TelegramQml::authSendInvites_slt));
    ASSERT(connect( p->telegram, &Telegram::authSignInError, this, &TelegramQml::authSignInError_slt));
    ASSERT(connect( p->telegram, &Telegram::authSignUpError, this, &TelegramQml::authSignUpError_slt));
    ASSERT(connect( p->telegram, &Telegram::accountRegisterDeviceAnswer, this, &TelegramQml::accountRegisterDevice_slt));
    ASSERT(connect( p->telegram, &Telegram::accountUnregisterDeviceAnswer, this, &TelegramQml::accountUnregisterDevice_slt));
    ASSERT(connect( p->telegram, &Telegram::error, this, &TelegramQml::error_slt));
    ASSERT(connect( p->telegram, &Telegram::connected, this, &TelegramQml::connectedChanged));
    ASSERT(connect( p->telegram, &Telegram::disconnected, this, &TelegramQml::connectedChanged));
    ASSERT(connect( p->telegram, &Telegram::authCheckPasswordAnswer, this, &TelegramQml::authCheckPassword_slt));

    ASSERT(connect( p->telegram, &Telegram::accountGetWallPapersAnswer, this, &TelegramQml::accountGetWallPapers_slt));
    ASSERT(connect( p->telegram, &Telegram::accountGetPasswordAnswer, this, &TelegramQml::accountGetPassword_slt));
    ASSERT(connect( p->telegram, &Telegram::accountCheckUsernameAnswer, this, &TelegramQml::accountCheckUsername_slt));
    ASSERT(connect( p->telegram, &Telegram::accountUpdateUsernameAnswer, this, &TelegramQml::accountUpdateUsername_slt));

    ASSERT(connect( p->telegram, &Telegram::contactsImportContactsAnswer, this, &TelegramQml::contactsImportContacts_slt));
    ASSERT(connect( p->telegram, &Telegram::photosUploadProfilePhotoAnswer, this, &TelegramQml::photosUploadProfilePhoto_slt));
    ASSERT(connect( p->telegram, &Telegram::photosUpdateProfilePhotoAnswer, this, &TelegramQml::photosUpdateProfilePhoto_slt));

    ASSERT(connect( p->telegram, &Telegram::contactsBlockAnswer, this, &TelegramQml::contactsBlock_slt));
    ASSERT(connect( p->telegram, &Telegram::contactsUnblockAnswer, this, &TelegramQml::contactsUnblock_slt));

    ASSERT(connect( p->telegram, &Telegram::usersGetFullUserAnswer, this, &TelegramQml::usersGetFullUser_slt));
    ASSERT(connect( p->telegram, &Telegram::usersGetUsersAnswer, this, &TelegramQml::usersGetUsers_slt));

    ASSERT(connect( p->telegram, &Telegram::messagesGetDialogsAnswer, this, &TelegramQml::messagesGetDialogs_slt));
    ASSERT(connect( p->telegram, &Telegram::messagesGetHistoryAnswer, this, &TelegramQml::messagesGetHistory_slt));
    ASSERT(connect( p->telegram, &Telegram::messagesReadHistoryAnswer,  this, &TelegramQml::messagesReadHistory_slt));
    ASSERT(connect( p->telegram, &Telegram::messagesReadEncryptedHistoryAnswer, this, &TelegramQml::messagesReadEncryptedHistory_slt));
    ASSERT(connect( p->telegram, &Telegram::messagesGetMessagesAnswer, this, &TelegramQml::messagesGetMessages_slt));

    ASSERT(connect( p->telegram, &Telegram::messagesSendMessageAnswer, this, &TelegramQml::messagesSendMessage_slt));
    ASSERT(connect( p->telegram, &Telegram::messagesForwardMessageAnswer, this, &TelegramQml::messagesForwardMessage_slt));
    ASSERT(connect( p->telegram, &Telegram::messagesForwardMessagesAnswer, this, &TelegramQml::messagesForwardMessages_slt));
    ASSERT(connect( p->telegram, &Telegram::messagesDeleteMessagesAnswer, this, &TelegramQml::messagesDeleteMessages_slt));
    ASSERT(connect( p->telegram, &Telegram::messagesDeleteHistoryAnswer, this, &TelegramQml::messagesDeleteHistory_slt));

    ASSERT(connect( p->telegram, &Telegram::messagesSearchAnswer, this, &TelegramQml::messagesSearch_slt));

    ASSERT(connect( p->telegram, &Telegram::messagesSendMediaAnswer, this, &TelegramQml::messagesSendMedia_slt));

    ASSERT(connect( p->telegram, &Telegram::messagesGetFullChatAnswer, this, &TelegramQml::messagesGetFullChat_slt));
    ASSERT(connect( p->telegram, &Telegram::messagesCreateChatAnswer, this, &TelegramQml::messagesCreateChat_slt));
    ASSERT(connect( p->telegram, &Telegram::messagesEditChatTitleAnswer, this, &TelegramQml::messagesEditChatTitle_slt));
    ASSERT(connect( p->telegram, &Telegram::messagesAddChatUserAnswer, this, &TelegramQml::messagesAddChatUser_slt));
    ASSERT(connect( p->telegram, &Telegram::messagesDeleteChatUserAnswer, this, &TelegramQml::messagesDeleteChatUser_slt));

    ASSERT(connect( p->telegram, &Telegram::messagesCreateEncryptedChatAnswer, this, &TelegramQml::messagesCreateEncryptedChat_slt));
    ASSERT(connect( p->telegram, &Telegram::messagesEncryptedChatRequested, this, &TelegramQml::messagesEncryptedChatRequested_slt));
    ASSERT(connect( p->telegram, &Telegram::messagesEncryptedChatDiscarded, this, &TelegramQml::messagesEncryptedChatDiscarded_slt));
    ASSERT(connect( p->telegram, &Telegram::messagesEncryptedChatCreated, this, &TelegramQml::messagesEncryptedChatCreated_slt));
    ASSERT(connect( p->telegram, &Telegram::messagesSendEncryptedAnswer, this, &TelegramQml::messagesSendEncrypted_slt));
    ASSERT(connect( p->telegram, &Telegram::messagesSendEncryptedFileAnswer, this, &TelegramQml::messagesSendEncryptedFile_slt));

    ASSERT(connect( p->telegram, &Telegram::contactsGetContactsAnswer, this, &TelegramQml::contactsGetContacts_slt));

    ASSERT(connect( p->telegram, &Telegram::channelsGetDialogsAnswer, this, &TelegramQml::channelsGetDialogs_slt));
    ASSERT(connect( p->telegram, &Telegram::channelsGetFullChannelAnswer, this, &TelegramQml::messagesGetFullChat_slt));
    ASSERT(connect( p->telegram, &Telegram::channelsGetImportantHistoryAnswer, this, &TelegramQml::messagesGetHistory_slt));
    ASSERT(connect( p->telegram, &Telegram::channelsGetMessagesAnswer, this, &TelegramQml::messagesGetMessages_slt));

    ASSERT(connect( p->telegram, &Telegram::updates, this, &TelegramQml::updates_slt));
    ASSERT(connect( p->telegram, &Telegram::updatesCombined, this, &TelegramQml::updatesCombined_slt));
    ASSERT(connect( p->telegram, &Telegram::updateShort, this, &TelegramQml::updateShort_slt));
    ASSERT(connect( p->telegram, &Telegram::updateShortChatMessage, this, &TelegramQml::updateShortChatMessage_slt));
    ASSERT(connect( p->telegram, &Telegram::updateShortMessage, this, &TelegramQml::updateShortMessage_slt));
    ASSERT(connect( p->telegram, &Telegram::updatesTooLong, this, &TelegramQml::updatesTooLong_slt));
    ASSERT(connect( p->telegram, &Telegram::updateSecretChatMessage, this, &TelegramQml::updateSecretChatMessage_slt));
    ASSERT(connect( p->telegram, &Telegram::updatesGetDifferenceAnswer, this, &TelegramQml::updatesGetDifference_slt));
    ASSERT(connect( p->telegram, &Telegram::updatesGetChannelDifferenceAnswer, this, &TelegramQml::updatesGetChannelDifference_slt));
    ASSERT(connect( p->telegram, &Telegram::updatesGetStateAnswer, this, &TelegramQml::updatesGetState_slt));

    ASSERT(connect( p->telegram, &Telegram::uploadGetFileAnswer, this, &TelegramQml::uploadGetFile_slt));
    ASSERT(connect( p->telegram, &Telegram::uploadCancelFileAnswer, this, &TelegramQml::uploadCancelFile_slt));
    ASSERT(connect( p->telegram, &Telegram::uploadSendFileAnswer, this, &TelegramQml::uploadSendFile_slt));

    ASSERT(connect( p->telegram, &Telegram::helpGetInviteTextAnswer, this, &TelegramQml::helpGetInviteTextAnswer));

    ASSERT(connect( p->telegram, &Telegram::fatalError, this, &TelegramQml::fatalError_slt, Qt::QueuedConnection));

    Q_EMIT telegramChanged();

    p->telegram->init();
    p->tsettings = p->telegram->settings();
    refreshSecretChats();
}

void TelegramQml::authNeeded_slt()
{
    p->authNeeded = true;
    p->authLoggedIn = false;

    Q_EMIT authNeededChanged();
    Q_EMIT authLoggedInChanged();
    Q_EMIT meChanged();

    if( p->telegram && !p->checkphone_req_id )
        p->checkphone_req_id = p->telegram->authCheckPhone();

//    p->telegram->accountUpdateStatus(!p->online || p->invisible);
}

void TelegramQml::authLoggedIn_slt()
{
    p->authNeeded = false;
    p->authLoggedIn = true;
    p->phoneChecked = true;
    p->checkphone_req_id = 0;
    p->authSignInCode.clear();

    Q_EMIT authNeededChanged();
    Q_EMIT authLoggedInChanged();
    Q_EMIT authPhoneCheckedChanged();
    Q_EMIT meChanged();

    QTimer::singleShot(1000, this, SLOT(updatesGetState()));
    timerUpdateContacts(1000);

}

void TelegramQml::authLogOut_slt(qint64 id, bool ok)
{
    Q_UNUSED(id)
    if(!ok)
        return;

    p->authNeeded = ok;
    p->authLoggedIn = !ok;
    p->logout_req_id = 0;

    Q_EMIT authNeededChanged();
    Q_EMIT authLoggedInChanged();
    Q_EMIT meChanged();
    Q_EMIT authLoggedOut();
}

void TelegramQml::authSendCode_slt(qint64 msgId, const AuthSentCode &result)
{
    Q_UNUSED(msgId)
    p->authNeeded = true;
    p->authLoggedIn = false;

    Q_EMIT authNeededChanged();
    Q_EMIT authLoggedInChanged();
    Q_EMIT authCodeRequested(result.phoneRegistered(), result.sendCallTimeout() );
}

void TelegramQml::authSendCodeError_slt(qint64 id)
{
    Q_UNUSED(id)
    p->telegram->authSendCode();
}

void TelegramQml::authSendCall_slt(qint64 id, bool ok)
{
    Q_UNUSED(id)
    Q_EMIT authCallRequested(ok);
}

void TelegramQml::authSendInvites_slt(qint64 id, bool ok)
{
    Q_UNUSED(id)
    Q_EMIT authInvitesSent(ok);
}

void TelegramQml::authCheckPassword_slt(qint64 id, const AuthAuthorization& result)
{
    Q_UNUSED(id)

    insertUser(result.user());
}

void TelegramQml::authCheckPhone_slt(qint64 id, const AuthCheckedPhone &result)
{
    p->checkphone_req_id = 0;
    QString phone = p->phoneCheckIds.take(id);

    if (phone.isEmpty()) {
        p->phoneRegistered = result.phoneRegistered();
        p->phoneInvited = false;
        p->phoneChecked = true;

        Q_EMIT authPhoneRegisteredChanged();
        Q_EMIT authPhoneInvitedChanged();
        Q_EMIT authPhoneCheckedChanged();

//        p->telegram->accountGetPassword();
        if(p->authSignInCode.isEmpty() || p->authCheckPhoneRetry>=3)
        {
            authSendCode();
        }
        else
        {
            qDebug() << __FUNCTION__ << "retrying..." << p->authCheckPhoneRetry;
            authSignIn(p->authSignInCode, true);
            p->authCheckPhoneRetry++;
        }
    } else {
        Q_EMIT phoneChecked(phone, result);
    }
}

void TelegramQml::authCheckPhoneError_slt(qint64 msgId, qint32 errorCode, const QString &errorText)
{
    Q_UNUSED(msgId)
    p->checkphone_req_id = p->telegram->authCheckPhone();
}

void TelegramQml::reconnect()
{
    if (p->telegram) {
        p->telegram->sleep();
        p->telegram->wake();
    }
}

void TelegramQml::accountGetPassword_slt(qint64 id, const AccountPassword &password)
{
    Q_UNUSED(id)
    //As a workaround for the binary corruption of the AccountPassword we store it here as a string, thereby guaranteeing deep copy
    //p->accountPassword = password;
    p->currentSalt = QString(password.currentSalt().toHex());
    if(password.classType() == AccountPassword::typeAccountPassword)
    {
        Q_EMIT authPasswordNeeded();
    }
    else
        authSendCode();

}

void TelegramQml::accountRegisterDevice_slt(qint64 id, bool result)
{
    Q_UNUSED(id);
    if (!result) {
        p->userdata->setPushToken("");
    }
    Q_EMIT accountDeviceRegistered(result);
}

void TelegramQml::accountUnregisterDevice_slt(qint64 id, bool result)
{
    Q_UNUSED(id);
    Q_UNUSED(result);
    Q_EMIT accountDeviceUnregistered(result);
    // regardless of result, since false may happen if we're not registered
    if (p->loggingOut) {
        p->logout_req_id = p->telegram->authLogOut();
    }
}

void TelegramQml::authSignInError_slt(qint64 id, qint32 errorCode, QString errorText)
{
    Q_UNUSED(id)

    qDebug() << __FUNCTION__ << errorText;
    if(errorCode == -1)
    {
        qDebug() << __FUNCTION__ << "Sign in retrying...";
        p->telegram->authSignIn(p->authSignInCode);
    }
    else
    {
        p->authSignUpError = "";
        p->authSignInError = errorText;
        p->authNeeded = true;
        Q_EMIT authNeededChanged();
        if(errorCode == 401 || errorText == "SESSION_PASSWORD_NEEDED")
        {
            p->telegram->accountGetPassword();
        }
        else
        {
            Q_EMIT authSignInErrorChanged();
            Q_EMIT authSignUpErrorChanged();
        }
    }
}

void TelegramQml::authSignUpError_slt(qint64 id, qint32 errorCode, QString errorText)
{
    Q_UNUSED(id)
    Q_UNUSED(errorCode)

    p->authSignUpError = errorText;
    p->authSignInError = "";
    p->authNeeded = true;
    Q_EMIT authNeededChanged();
    Q_EMIT authSignInErrorChanged();
    Q_EMIT authSignUpErrorChanged();

    qDebug() << __FUNCTION__ << errorText;
}

void TelegramQml::error_slt(qint64 id, qint32 errorCode, QString errorText, QString functionName)
{
    Q_UNUSED(id)
    Q_UNUSED(errorCode)

    p->error = errorText;
    Q_EMIT errorChanged();

    if(errorText.contains("PHONE_PASSWORD_PROTECTED"))
        Q_EMIT authPasswordProtectedError();

    if(errorText.contains("PEER_ID_INVALID") &&
       functionName.contains("messagesDeleteHistory")) {
        messagesDeleteHistory_slt(id, MessagesAffectedHistory());
    }

    qDebug() << __FUNCTION__ << errorCode << errorText << functionName;

    Q_EMIT errorSignal(id, errorCode, errorText, functionName);
}

void TelegramQml::accountGetWallPapers_slt(qint64 id, const QList<WallPaper> &wallPapers)
{
    Q_UNUSED(id)

    Q_FOREACH( const WallPaper & wp, wallPapers )
    {
        if( p->wallpapers_map.contains(wp.id()) )
            continue;

        WallPaperObject *obj = new WallPaperObject(wp, this);
        p->wallpapers_map[wp.id()] = obj;

        PhotoSizeObject *sml_size = obj->sizes()->last();
        if( sml_size )
            getFile(sml_size->location());

        PhotoSizeObject *lrg_size = obj->sizes()->first();
        if( lrg_size )
            getFileJustCheck(lrg_size->location());
    }

    Q_EMIT wallpapersChanged();
}

void TelegramQml::accountCheckUsername_slt(qint64 id, bool ok)
{
    Q_UNUSED(id);

    Q_EMIT accountUsernameChecked(ok);
}

void TelegramQml::accountUpdateUsername_slt(qint64 id, const User &user)
{
    Q_UNUSED(id);

    insertUser(user);
}

void TelegramQml::photosUploadProfilePhoto_slt(qint64 id, const PhotosPhoto &result)
{
    Q_UNUSED(id)

    p->telegram->photosUpdateProfilePhoto(result.photo().id(), result.photo().accessHash());

    UserObject *user = p->users.value(me());
    if(user)
    {
        user->photo()->photoBig()->download()->setLocation(FILES_PRE_STR + p->upload_photo_path );
        user->photo()->photoSmall()->download()->setLocation(FILES_PRE_STR + p->upload_photo_path );
        p->upload_photo_path.clear();
    }

    Q_FOREACH( const User & user, result.users() )
        insertUser(user);

    p->profile_upload_id = 0;
    Q_EMIT uploadingProfilePhotoChanged();
}

void TelegramQml::photosUpdateProfilePhoto_slt(qint64 id, const UserProfilePhoto &userProfilePhoto)
{
    Q_UNUSED(id)

    UserObject *user = p->users.value(me());
    if(user)
        *(user->photo()) = userProfilePhoto;

    timerUpdateDialogs(100);
}

void TelegramQml::contactsBlock_slt(qint64 id, bool ok)
{
    qint64 userId = p->blockRequests.take(id);
    if (ok) blockUser(userId);
}

void TelegramQml::contactsUnblock_slt(qint64 id, bool ok)
{
    qint64 userId = p->unblockRequests.take(id);
    if (ok) unblockUser(userId);
}

void TelegramQml::contactsImportContacts_slt(qint64 id, const ContactsImportedContacts &result)
{
    Q_UNUSED(id)

    Q_FOREACH( const User & user, result.users() )
        insertUser(user);

    timerUpdateDialogs(100);
    timerUpdateContacts(100);

    Q_EMIT contactsImportedContacts(result.imported().length(), result.retryContacts().length());
}

void TelegramQml::contactsGetContacts_slt(qint64 id, const ContactsContacts &result)
{
    Q_UNUSED(id)

    if (result.classType() == ContactsContacts::ContactsContactsClassType::typeContactsContacts)
    {
    Q_FOREACH( const User & user, result.users() )
        insertUser(user);
    Q_FOREACH( const Contact & contact, result.contacts() )
        insertContact(contact);
    }
}

void TelegramQml::usersGetFullUser_slt(qint64 id, const UserFull &result)
{
    Q_UNUSED(id)
    insertUser(result.user());
    if (result.blocked()) {
        blockUser(result.user().id());
    } else {
        unblockUser(result.user().id());
    }
}

void TelegramQml::usersGetUsers_slt(qint64 id, const QList<User> &users)
{
    Q_UNUSED(id)
    Q_FOREACH( const User & user, users )
        insertUser(user);
}

void TelegramQml::messagesSendMessage_slt(qint64 id, const UpdatesType &result)
{

    if( !p->pend_messages.contains(id) )
        return;

    MessageObject *msgObj = p->pend_messages.take(id);
    msgObj->setSent(true);

    qint64 old_msgId = msgObj->unifiedId();

    Peer peer(static_cast<Peer::PeerClassType>(msgObj->toId()->classType()));
    if (peer.classType() == Peer::typePeerChat)
        peer.setChatId(msgObj->toId()->chatId());
    else if (peer.classType() == Peer::typePeerChannel)
        peer.setChannelId(msgObj->toId()->channelId());
    else
        peer.setUserId(msgObj->toId()->userId());

    Message msg(Message::typeMessage);
    msg.setFromId(msgObj->fromId());
    msg.setId(result.id());
    msg.setDate(result.date());
    msg.setFlags(UNREAD_OUT_TO_FLAG(msgObj->unread(), msgObj->out()));
    msg.setToId(peer);
    msg.setMessage(msgObj->message());
    msg.setReplyToMsgId(msgObj->replyToMsgId());
    msg.setMedia(result.media());

    auto unifiedId = QmlUtils::getUnifiedMessageKey(msg.id(), msg.toId().channelId());
    qint64 did = msg.toId().chatId();
    if( !did )
        did = msg.toId().channelId();
    if( !did )
        did = msgObj->out()? msg.toId().userId() : msg.fromId();

    insertToGarbeges(p->messages.value(old_msgId));
    if(msg.id() != 0)
    {
        insertMessage(msg);
        timerUpdateDialogs(3000);

        Q_EMIT messageSent(id, p->messages.value(unifiedId));
    }
    else
    {
        p->database->deleteMessage(old_msgId);
        Q_EMIT messagesChanged(false);
    }
    Q_EMIT messagesSent(1);
}

void TelegramQml::messagesForwardMessage_slt(qint64 id, const UpdatesType &updates)
{
    Q_UNUSED(id)
    insertUpdates(updates);
}

void TelegramQml::messagesForwardMessages_slt(qint64 id, const UpdatesType &updates)
{
    Q_UNUSED(id)
    insertUpdates(updates);

    Q_EMIT messagesSent(updates.updates().length());
}

void TelegramQml::messagesDeleteMessages_slt(qint64 id, const MessagesAffectedMessages &deletedMessages)
{
    Q_UNUSED(id)
    Q_UNUSED(deletedMessages)

    Q_EMIT messagesChanged(false);
    timerUpdateDialogs(3000);
}

void TelegramQml::messagesGetMessages_slt(qint64 id, const MessagesMessages &result)
{
    Q_UNUSED(id)

    Q_FOREACH( const Chat & chat, result.chats() )
        insertChat(chat);
    Q_FOREACH( const User & user, result.users() )
        insertUser(user);
    Q_FOREACH( const Message & message, result.messages() )
        insertMessage(message);
}

void TelegramQml::messagesSendMedia_slt(qint64 id, const UpdatesType &updates)
{
    MessageObject *uplMsg = p->uploads.value(id);
    if(!uplMsg)
        return;

    qint64 old_msgId = uplMsg->unifiedId();

    MessageObject* msg;
    msg = p->messages.value(old_msgId);
    insertToGarbeges(msg);
    insertUpdates(updates);
    timerUpdateDialogs(3000);

    Q_EMIT messagesSent(1);
}

void TelegramQml::messagesForwardMedia_slt(qint64 id, const UpdatesType &updates)
{
    Q_UNUSED(id)
    insertUpdates(updates);
    timerUpdateDialogs(3000);
}

void TelegramQml::messagesForwardPhoto_slt(qint64 id, const UpdatesType &updates)
{
    Q_UNUSED(id)
    insertUpdates(updates);
    timerUpdateDialogs(3000);
}

void TelegramQml::messagesForwardVideo_slt(qint64 id, const UpdatesType &updates)
{
    Q_UNUSED(id)
    insertUpdates(updates);
    timerUpdateDialogs(3000);
}

void TelegramQml::messagesForwardAudio_slt(qint64 id, const UpdatesType &updates)
{
    Q_UNUSED(id)
    insertUpdates(updates);
    timerUpdateDialogs(3000);
}

void TelegramQml::messagesForwardDocument_slt(qint64 id, const UpdatesType &updates)
{
    Q_UNUSED(id)
    insertUpdates(updates);
    timerUpdateDialogs(3000);

    Q_EMIT messagesSent(1);
}

void TelegramQml::channelsGetDialogs_slt(qint64 id, const MessagesDialogs &result)
{
    Q_UNUSED(id)
    getDialogsLock.lock();
    Q_FOREACH( const User & u, result.users() )
        insertUser(u);
    Q_FOREACH( const Chat & c, result.chats() )
        insertChat(c);
    Q_FOREACH( const Message & m, result.messages() )
        insertMessage(m);

    //Initialize a list of all existing dialogs to check if one or more were deleted
    //This is kind of reverse logic!
    QSet<qint64> deletedChannels;
    for (QHash<qint64, DialogObject*>::iterator it = p->dialogs.begin(); it != p->dialogs.end(); ++it)
    {
        if(it.value()->classType() == Dialog::typeDialogChannel)
            deletedChannels.insert(it.key());
    }

    Q_FOREACH( const Dialog & d, result.dialogs() )
    {
        insertDialog(d);
        qint64 dialogId = d.peer().channelId()? d.peer().channelId() : d.peer().chatId()? d.peer().chatId() : d.peer().userId();
        //Remove this dialog from deleted dialogs, it is still valid
        deletedChannels.remove(dialogId);
    }

    //The remaining dialogs in deletedDialogs can now be safely deleted also locally
    if(p->database) {
        Q_FOREACH(qint64 dId, deletedChannels)
        {
            if(p->dialogs[dId]->encrypted())
                continue;

            p->database->deleteDialog(dId);
            insertToGarbeges(p->dialogs.value(dId));
        }
    }
    getDialogsLock.unlock();
    Q_EMIT dialogsChanged(false);
}

void TelegramQml::messagesGetDialogs_slt(qint64 id, const MessagesDialogs &result)
{
    Q_UNUSED(id)

    getDialogsLock.lock();
    Q_FOREACH( const User & u, result.users() )
        insertUser(u);
    Q_FOREACH( const Chat & c, result.chats() )
        insertChat(c);
    Q_FOREACH( const Message & m, result.messages() )
        insertMessage(m);

    //Initialize a list of all existing dialogs to check if one or more were deleted
    //This is kind of reverse logic!
    QSet<qint64> deletedDialogs;
    for (QHash<qint64, DialogObject*>::iterator it = p->dialogs.begin(); it != p->dialogs.end(); ++it)
    {
        if(it.value()->classType() == Dialog::typeDialog)
            deletedDialogs.insert(it.key());
    }

    Q_FOREACH( const Dialog & d, result.dialogs() )
    {
        insertDialog(d);
        qint64 dialogId = d.peer().channelId()? d.peer().channelId() : d.peer().chatId()? d.peer().chatId() : d.peer().userId();
        //Remove this dialog from deleted dialogs, it is still valid
        deletedDialogs.remove(dialogId);
    }

    //The remaining dialogs in deletedDialogs can now be safely deleted also locally
    if(p->database) {
        Q_FOREACH(qint64 dId, deletedDialogs)
        {
            if(p->dialogs[dId]->encrypted())
                continue;

            p->database->deleteDialog(dId);
            insertToGarbeges(p->dialogs.value(dId));
        }
    }
    getDialogsLock.unlock();
    Q_EMIT dialogsChanged(false);
    refreshSecretChats();
}

void TelegramQml::messagesGetHistory_slt(qint64 id, const MessagesMessages &result)
{
    Q_UNUSED(id)

    Q_FOREACH( const User & u, result.users() )
        insertUser(u);
    Q_FOREACH( const Chat & c, result.chats() )
        insertChat(c);
    Q_FOREACH( const Message & m, result.messages() )
        insertMessage(m);

    Q_EMIT messagesChanged(false);
}

void TelegramQml::messagesReadHistory_slt(qint64 id, const MessagesAffectedMessages &result)
{
    Q_UNUSED(result);

    qint64 peerId = p->read_history_requests.take(id);
    if (peerId)
    {
        DialogObject *dialog = p->dialogs.value(peerId);
        if (dialog)
        {
            dialog->setUnreadCount(0);
            p->database->updateUnreadCount(peerId, 0);
            Q_EMIT dialogsChanged(false);
        }
    }

    peerId = p->delete_history_requests.take(id);
    if (peerId) {
        // No need to pass deleteChat value here, it's derived from deleteChatIds.
        messagesDeleteHistory(peerId);
    }
}

void TelegramQml::messagesReadEncryptedHistory_slt(qint64 id, bool ok)
{
    if(ok)
        messagesReadHistory_slt(id, MessagesAffectedMessages());
}

void TelegramQml::messagesDeleteHistory_slt(qint64 id, const MessagesAffectedHistory &result)
{
    qint64 peerId = p->delete_history_requests.take(id);
    if (peerId)
    {
        deleteLocalHistory(peerId);
    }
}

void TelegramQml::messagesSearch_slt(qint64 id, const MessagesMessages &result)
{
    Q_UNUSED(id)

    QList<qint64> res;

    Q_FOREACH( const User & u, result.users() )
        insertUser(u);
    Q_FOREACH( const Chat & c, result.chats() )
        insertChat(c);
    Q_FOREACH( const Message & m, result.messages() )
    {
        insertMessage(m);
        auto unifiedId = QmlUtils::getUnifiedMessageKey(m.id(), m.toId().channelId());
        res << unifiedId;
    }

    Q_EMIT searchDone(res);
}

void TelegramQml::messagesGetFullChat_slt(qint64 id, const MessagesChatFull &result)
{
    Q_UNUSED(id)
    Q_FOREACH( const User & u, result.users() )
        insertUser(u);
    Q_FOREACH( const Chat & c, result.chats() )
        insertChat(c);

    ChatFullObject *fullChat = p->chatfulls.value(result.fullChat().id());
    if( !fullChat )
    {
        fullChat = new ChatFullObject(result.fullChat(), this);
        p->chatfulls.insert(result.fullChat().id(), fullChat);
    }
    else
        *fullChat = result.fullChat();

    //Some channel debugging
    if (fullChat->classType() == static_cast<qint32>(ChatFull::typeChannelFull))
    {
        qWarning() << "Found full channel id: " << fullChat->id() << ", no of participants: " << result.fullChat().participantsCount();
    }
    //If we are coming from deletion of a conversation, execute additional steps
    qint64 peerId = result.fullChat().id();
    if(p->deleteChatIds.contains(peerId)) {
        ChatParticipantsObject* object = fullChat->participants();
        ChatParticipantList* list = object->participants();
        QList<qint64> uids = list->userIds();

        if (uids.contains(me())) {
            messagesDeleteChatUser(peerId, me());
        } else {
            messagesDeleteHistory(peerId, true, true);
        }
    }

    Q_EMIT chatFullsChanged();
}

void TelegramQml::messagesCreateChat_slt(qint64 id, const UpdatesType &updates)
{
    Q_UNUSED(id)
    insertUpdates(updates);
}

void TelegramQml::messagesEditChatTitle_slt(qint64 id, const UpdatesType &updates)
{
    Q_UNUSED(id)
    insertUpdates(updates);
}

void TelegramQml::messagesEditChatPhoto_slt(qint64 id, const UpdatesType &updates)
{
    Q_UNUSED(id)
    insertUpdates(updates);
}

void TelegramQml::messagesAddChatUser_slt(qint64 id, const UpdatesType &updates)
{
    Q_UNUSED(id)
    insertUpdates(updates);
}

void TelegramQml::messagesDeleteChatUser_slt(qint64 id, const UpdatesType &updates)
{
    Q_UNUSED(id)
    insertUpdates(updates);

    QList<Update> updatesList = updates.updates();
    updatesList << updates.update();

    Q_FOREACH(const Update &upd, updatesList)
    {
        const Message &message = upd.message();
        qint64 peerId = message.toId().chatId();
        if (!peerId)
            peerId = message.toId().channelId();
        if(peerId && p->deleteChatIds.contains(peerId)) {
            messagesDeleteHistory(peerId, true, true);
        }
    }

    timerUpdateDialogs(500);
}

void TelegramQml::messagesCreateEncryptedChat_slt(qint32 chatId, qint32 date, qint32 peerId, qint64 accessHash)
{
    EncryptedChat c(EncryptedChat::typeEncryptedChatWaiting);
    c.setId(chatId);
    c.setAccessHash(accessHash);
    c.setAdminId(me());
    c.setParticipantId(peerId);
    c.setDate(date);

    insertEncryptedChat(c);
}

void TelegramQml::messagesEncryptedChatRequested_slt(qint32 chatId, qint32 date, qint32 creatorId, qint64 creatorAccessHash)
{
    EncryptedChat c(EncryptedChat::typeEncryptedChatRequested);
    c.setId(chatId);
    c.setAdminId(creatorId);
    c.setParticipantId(me());
    c.setDate(date);

    if(!p->users.contains(creatorId))
    {
        User u = User();
        u.setId(creatorId);
        u.setAccessHash(creatorAccessHash);

        insertUser(u);
    }

    insertEncryptedChat(c);

    if (p->autoAcceptEncrypted) {
        p->telegram->messagesAcceptEncryptedChat(chatId);
    }
}

void TelegramQml::messagesEncryptedChatCreated_slt(qint32 chatId)
{
    EncryptedChatObject *c = p->encchats.value(chatId);
    if(!c)
        return;

    c->setClassType(EncryptedChat::typeEncryptedChat);
}

void TelegramQml::messagesEncryptedChatDiscarded_slt(qint32 chatId)
{
    EncryptedChatObject *c = p->encchats.value(chatId);
    if(!c)
        return;

    c->setClassType(EncryptedChat::typeEncryptedChatDiscarded);

    if (p->deleteChatIds.contains(chatId)) {
        deleteLocalHistory(chatId);
    }
}

void TelegramQml::messagesSendEncrypted_slt(qint64 id, qint32 date, const EncryptedFile &encryptedFile)
{
    Q_UNUSED(encryptedFile)
    if( !p->pend_messages.contains(id) )
        return;

    MessageObject *msgObj = p->pend_messages.take(id);
    msgObj->setSent(true);

    qint64 old_msgId = msgObj->unifiedId();

    Peer peer(static_cast<Peer::PeerClassType>(msgObj->toId()->classType()));
    if (peer.classType() == Peer::typePeerChat)
        peer.setChatId(msgObj->toId()->chatId());
    else if (peer.classType() == Peer::typePeerChannel)
        peer.setChannelId(msgObj->toId()->channelId());
    else
        peer.setUserId(msgObj->toId()->userId());

    Message msg(Message::typeMessage);
    msg.setFromId(msgObj->fromId());
    msg.setId(date);
    msg.setDate(date);
    msg.setFlags(UNREAD_OUT_TO_FLAG(msgObj->unread(), msgObj->out()));
    msg.setToId(peer);
    msg.setMessage(msgObj->message());

    qint64 did = msg.toId().chatId();
    if ( !did )
        did = msg.toId().channelId();
    if( !did )
        did = FLAG_TO_OUT(msg.flags())? msg.toId().userId() : msg.fromId();

    insertToGarbeges(p->messages.value(old_msgId));
    insertMessage(msg);
    timerUpdateDialogs(3000);
}

void TelegramQml::messagesSendEncryptedFile_slt(qint64 id, qint32 date, const EncryptedFile &encryptedFile)
{
    MessageObject *msgObj = p->uploads.take(id);
    if(!msgObj)
        return;

    UploadObject *upload = msgObj->upload();
    FileLocation location(FileLocation::typeFileLocation);
    FileLocationObject locationObj(location, msgObj);
    locationObj.setId(encryptedFile.id());
    locationObj.setDcId(encryptedFile.dcId());
    locationObj.setAccessHash(encryptedFile.accessHash());

    const QString &srcFile = upload->location();
    const QString &dstFile = fileLocation(&locationObj);
    QString srcSuffix = QFileInfo(srcFile).suffix();
    if(!srcSuffix.isEmpty())
        srcSuffix = "." + srcSuffix;

    QFile::copy(srcFile, dstFile + srcSuffix);

    msgObj->setSent(true);

    qint64 old_msgId = msgObj->unifiedId();

    Peer peer(static_cast<Peer::PeerClassType>(msgObj->toId()->classType()));
    if (peer.classType() == Peer::typePeerChat)
        peer.setChatId(msgObj->toId()->chatId());
    else if (peer.classType() == Peer::typePeerChannel)
        peer.setChannelId(msgObj->toId()->chatId());
    else
        peer.setUserId(msgObj->toId()->userId());

    Dialog dialog;
    dialog.setPeer(peer);
    //FIXME: Why the date is set as top Message???
    dialog.setTopMessage(msgObj->id());
    dialog.setUnreadCount(0);

    MessageMediaObject *mediaObj = msgObj->media();
    MessageMedia media( static_cast<MessageMedia::MessageMediaClassType>(mediaObj->classType()) );
    switch(mediaObj->classType())
    {
    case MessageMedia::typeMessageMediaPhoto:
    {
        QImageReader reader(srcFile);

        PhotoSize psize(PhotoSize::typePhotoSize);
        psize.setH(reader.size().height());
        psize.setW(reader.size().width());
        psize.setSize(QFileInfo(srcFile).size());

        Photo photo(Photo::typePhoto);
        photo.setAccessHash(encryptedFile.accessHash());
        photo.setId(encryptedFile.id());
        photo.setDate(date);
        photo.setSizes( QList<PhotoSize>()<<psize );

        media.setPhoto(photo);
    }
        break;

    case MessageMedia::typeMessageMediaVideo:
    {
        Video video(Video::typeVideo);
        video.setAccessHash(encryptedFile.accessHash());
        video.setId(encryptedFile.id());
        video.setDate(date);
        video.setSize(encryptedFile.size());
        video.setDcId(encryptedFile.dcId());
        video.setW(640);
        video.setH(400);
        media.setVideo(video);
    }
        break;

    case MessageMedia::typeMessageMediaAudio:
    {
        Audio audio(Audio::typeAudio);
        audio.setAccessHash(encryptedFile.accessHash());
        audio.setId(encryptedFile.id());
        audio.setDate(date);
        audio.setSize(encryptedFile.size());
        audio.setDcId(encryptedFile.dcId());
        media.setAudio(audio);
    }
        break;

    default:
    case MessageMedia::typeMessageMediaDocument:
    {
        Document document(Document::typeDocument);
        document.setAccessHash(encryptedFile.accessHash());
        document.setId(encryptedFile.id());
        document.setDate(date);
        document.setSize(encryptedFile.size());
        document.setDcId(encryptedFile.dcId());
        media.setDocument(document);
    }
        break;
    }

    Message msg(Message::typeMessage);
    msg.setFromId(msgObj->fromId());
    msg.setId(date);
    msg.setDate(date);
    msg.setFlags(UNREAD_OUT_TO_FLAG(msgObj->unread(), msgObj->out()));
    msg.setMedia(media);
    msg.setToId(peer);
    msg.setMessage(msgObj->message());

    qint64 did = msg.toId().chatId();
    if( !did )
        did = msg.toId().channelId();
    if( !did )
        did = FLAG_TO_OUT(msg.flags())? msg.toId().userId() : msg.fromId();

    insertToGarbeges(p->messages.value(old_msgId));
    insertMessage(msg, true);
    insertDialog(dialog, true);
    timerUpdateDialogs(3000);
}

void TelegramQml::messagesGetStickers_slt(qint64 msgId, const MessagesStickers &stickers)
{
    Q_UNUSED(msgId)

    const QList<Document> &documents = stickers.stickers();
    Q_FOREACH(const Document &doc, documents)
        insertDocument(doc);
}

void TelegramQml::messagesGetAllStickers_slt(qint64 msgId, const MessagesAllStickers &stickers)
{
    Q_UNUSED(msgId)

    p->installedStickerSets.clear();

    const QList<StickerSet> &sets = stickers.sets();
    Q_FOREACH(const StickerSet &set, sets)
    {
        insertStickerSet(set);
        p->installedStickerSets.insert(set.id());
        p->stickerShortIds[set.shortName()] = set.id();
    }


    Q_EMIT installedStickersChanged();
}

void TelegramQml::messagesGetStickerSet_slt(qint64 msgId, const MessagesStickerSet &stickerset)
{
    Q_UNUSED(msgId)
    const QList<Document> &documents = stickerset.documents();
    Q_FOREACH(const Document &doc, documents)
    {
        insertDocument(doc);

        const QList<DocumentAttribute> &attrs = doc.attributes();
        Q_FOREACH(const DocumentAttribute &attr, attrs)
            if(attr.classType() == DocumentAttribute::typeDocumentAttributeSticker)
                p->stickersMap[attr.stickerset().id()].insert(doc.id());
    }

    const QList<StickerPack> &packs = stickerset.packs();
    Q_FOREACH(const StickerPack &pack, packs)
        insertStickerPack(pack);

    StickerSet set = stickerset.set();

    insertStickerSet(set);
    p->stickerShortIds[set.shortName()] = set.id();

    Q_EMIT stickerRecieved(stickerset.set().id());
    if(p->pending_doc_stickers.contains(msgId))
        Q_EMIT documentStickerRecieved(p->pending_doc_stickers.take(msgId), p->stickerSets.value(set.id()));
}

void TelegramQml::messagesInstallStickerSet_slt(qint64 msgId, bool ok)
{
    Q_UNUSED(msgId)

    const QString &shortId = p->pending_stickers_install.take(msgId);
    if(ok)
    {
        const qint64 stickerId = p->stickerShortIds.value(shortId);
        if(stickerId)
        {
            p->installedStickerSets.insert(stickerId);
            Q_EMIT installedStickersChanged();
        }
        else
            p->telegram->messagesGetAllStickers(QString());
    }

    Q_EMIT stickerInstalled(shortId, ok);
}

void TelegramQml::messagesUninstallStickerSet_slt(qint64 msgId, bool ok)
{
    const QString &shortId = p->pending_stickers_uninstall.take(msgId);
    if(ok)
    {
        const qint64 stickerId = p->stickerShortIds.value(shortId);
        if(!stickerId)
            return;

        p->installedStickerSets.remove(stickerId);
        Q_EMIT installedStickersChanged();
    }

    Q_EMIT stickerUninstalled(shortId, ok);
}

void TelegramQml::updatesTooLong_slt()
{
    timerUpdateDialogs();
}

void TelegramQml::updateShortMessage_slt(qint32 id, qint32 userId, const QString &message, qint32 pts, qint32 pts_count, qint32 date, Peer fwd_from_id, qint32 fwd_date, qint32 reply_to_msg_id, bool unread, bool out)
{

    qWarning() << "Update pts: " << pts << ", pts count: " << pts_count;
    Peer to_peer(Peer::typePeerUser);
    to_peer.setUserId(out?userId:p->telegram->ourId());

    Message msg(Message::typeMessage);
    msg.setId(id);
    msg.setFromId(out?p->telegram->ourId():userId);
    msg.setMessage(message);
    msg.setDate(date);
    msg.setFlags(UNREAD_OUT_TO_FLAG(unread, out));
    msg.setToId(to_peer);
    msg.setFwdFromId(fwd_from_id);
    msg.setFwdDate(fwd_date);
    msg.setReplyToMsgId(reply_to_msg_id);

    auto unifiedId = QmlUtils::getUnifiedMessageKey(msg.id(), msg.toId().channelId());
    insertMessage(msg);
    if( p->dialogs.contains(userId) )
    {
        DialogObject *dlg_o = p->dialogs.value(userId);
        dlg_o->setTopMessage(id);
        dlg_o->setUnreadCount( dlg_o->unreadCount()+1 );
    }
    else
    {
        Peer fr_peer(Peer::typePeerUser);
        fr_peer.setUserId(userId);

        Dialog dlg;
        dlg.setPeer(fr_peer);
        dlg.setTopMessage(id);
        dlg.setUnreadCount(1);

        insertDialog(dlg);
    }

    timerUpdateDialogs(3000);

    Q_EMIT incomingMessage( p->messages.value(unifiedId) );

    if (!out) {
        Q_EMIT messagesReceived(1);
    }
}

void TelegramQml::updateShortChatMessage_slt(qint32 id, qint32 fromId, qint32 chatId, const QString &message, qint32 pts, qint32 pts_count, qint32 date, Peer fwd_from_id, qint32 fwd_date, qint32 reply_to_msg_id, bool unread, bool out)
{

    qWarning() << "Update pts: " << pts << ", pts count: " << pts_count;
    Peer to_peer(Peer::typePeerChat);
    to_peer.setChatId(chatId);

    Message msg(Message::typeMessage);
    msg.setId(id);
    msg.setFromId(fromId);
    msg.setMessage(message);
    msg.setDate(date);
    msg.setFlags(UNREAD_OUT_TO_FLAG(unread, out));
    msg.setToId(to_peer);
    msg.setFwdDate(fwd_date);
    msg.setFwdFromId(fwd_from_id);
    msg.setReplyToMsgId(reply_to_msg_id);

    auto unifiedId = QmlUtils::getUnifiedMessageKey(msg.id(), msg.toId().channelId());
    insertMessage(msg);
    if( p->dialogs.contains(chatId) )
    {
        DialogObject *dlg_o = p->dialogs.value(chatId);
        dlg_o->setTopMessage(id);
        dlg_o->setUnreadCount( dlg_o->unreadCount()+1 );
    }
    else
    {
        Dialog dlg;
        dlg.setPeer(to_peer);
        dlg.setTopMessage(id);
        dlg.setUnreadCount(1);

        insertDialog(dlg);
    }

    timerUpdateDialogs(3000);

    Q_EMIT incomingMessage( p->messages.value(unifiedId) );

    if (!out) {
        Q_EMIT messagesReceived(1);
    }
}

void TelegramQml::updateShort_slt(const Update &update, qint32 date)
{
    Q_UNUSED(date)
    insertUpdate(update);
}

void TelegramQml::updatesCombined_slt(const QList<Update> & updates, const QList<User> & users, const QList<Chat> & chats, qint32 date, qint32 seqStart, qint32 seq)
{
    Q_UNUSED(date)
    Q_UNUSED(seq)
    Q_UNUSED(seqStart)
    Q_FOREACH( const Update & u, updates )
        insertUpdate(u);
    Q_FOREACH( const User & u, users )
        insertUser(u);
    Q_FOREACH( const Chat & c, chats )
        insertChat(c);
}

void TelegramQml::updates_slt(const QList<Update> & updates, const QList<User> & users, const QList<Chat> & chats, qint32 date, qint32 seq)
{
    Q_UNUSED(date)
    Q_UNUSED(seq)
    Q_FOREACH( const Update & u, updates )
        insertUpdate(u);
    Q_FOREACH( const User & u, users )
        insertUser(u);
    Q_FOREACH( const Chat & c, chats )
        insertChat(c);
}

void TelegramQml::updateSecretChatMessage_slt(const SecretChatMessage &secretChatMessage, qint32 qts)
{
    Q_UNUSED(qts)
    insertSecretChatMessage(secretChatMessage);
}

void TelegramQml::updatesGetDifference_slt(qint64 id, const QList<Message> &messages, const QList<SecretChatMessage> &secretChatMessages, const QList<Update> &otherUpdates, const QList<Chat> &chats, const QList<User> &users, const UpdatesState &state, bool isIntermediateState)
{
    Q_UNUSED(id)
    Q_UNUSED(state)
    Q_UNUSED(isIntermediateState)

    // Count messages received today for basic stats.
    // On Ubuntu, they can be shown on the lock screen.
    qint32 receivedMessageCount = 0;
    QDate today = QDate::currentDate();

    Q_FOREACH( const Update & u, otherUpdates )
        insertUpdate(u);
    Q_FOREACH( const User & u, users )
        insertUser(u);
    Q_FOREACH( const Chat & c, chats )
        insertChat(c);
    Q_FOREACH( const Message & m, messages ) {
        insertMessage(m);

        if (!FLAG_TO_OUT(m.flags())) {
            QDate messageDate = QDateTime::fromTime_t(m.date()).date();
            if (today == messageDate) {
                receivedMessageCount += 1;
            }
        }
    }
    Q_FOREACH( const SecretChatMessage & m, secretChatMessages )
        insertSecretChatMessage(m, true);

    Q_EMIT messagesReceived(receivedMessageCount);
}

void TelegramQml::updatesGetChannelDifference_slt(qint64 msgId, const UpdatesChannelDifference &result)
{

}

void TelegramQml::updatesGetState_slt(qint64 msgId, const UpdatesState &result)
{
    Q_UNUSED(msgId)

    p->state.setDate(result.date());
    p->state.setPts(result.pts());
    p->state.setQts(result.qts());
    p->state.setSeq(result.seq());
    p->state.setUnreadCount(result.unreadCount());

    QTimer::singleShot(100, this, SLOT(updatesGetDifference()));
}

void TelegramQml::uploadGetFile_slt(qint64 id, const StorageFileType &type, qint32 mtime, const QByteArray & bytes, qint32 partId, qint32 downloaded, qint32 total)
{
    FileLocationObject *obj = p->downloads.value(id);
    if( !obj )
        return;
    if( !TqObject::isValid(tqobject_cast(obj)) )
    {
        p->downloads.remove(id);
        return;
    }

    Q_UNUSED(type)
    DownloadObject *download = obj->download();
    download->setMtime(mtime);
    download->setPartId(partId);
    download->setDownloaded(downloaded);

    const QString & download_file = download->file()->fileName().isEmpty()? fileLocation(obj) : download->file()->fileName();
    if(total)
        download->setTotal(total);

    if( !download->file()->isOpen() )
    {
        download->file()->setFileName(download_file);
        if( !download->file()->open(QFile::WriteOnly) )
            return;
    }

    download->file()->write(bytes);

    if( downloaded >= download->total() && total == downloaded )
    {
        download->file()->flush();
        download->file()->close();

        const QMimeType & t = p->mime_db.mimeTypeForFile(download_file);
        const QStringList & suffixes = t.suffixes();
        if( !suffixes.isEmpty() )
        {
            QString sfx = suffixes.first();
            if(!obj->fileName().isEmpty())
            {
                QFileInfo finfo(obj->fileName());
                if(!finfo.suffix().isEmpty())
                    sfx = finfo.suffix();
            }

            if(!sfx.isEmpty())
                sfx = "."+sfx;

            if (download_file.right(sfx.length()) != sfx) {
                QFile::rename(download_file, download_file+sfx);
                download->setLocation(FILES_PRE_STR + download_file+sfx);
            } else {
                download->setLocation(FILES_PRE_STR + download_file);
            }
        }
        else
            download->setLocation(FILES_PRE_STR + download_file);

        download->setFileId(0);
        p->downloads.remove(id);
    }
}

void TelegramQml::uploadSendFile_slt(qint64 fileId, qint32 partId, qint32 uploaded, qint32 totalSize)
{
    MessageObject *msgObj = p->uploads.value(fileId);
    if(!msgObj)
        return;

    UploadObject *upload = msgObj->upload();
    upload->setPartId(partId);
    upload->setUploaded(uploaded);
    upload->setTotalSize(totalSize);
}

void TelegramQml::uploadCancelFile_slt(qint64 fileId, bool cancelled)
{
    if(!cancelled)
        return;

    if( p->uploads.contains(fileId) )
    {
        MessageObject *msgObj = p->uploads.take(fileId);
        qint64 msgId = msgObj->unifiedId();

        insertToGarbeges(p->messages.value(msgId));
        Q_EMIT messagesChanged(false);
    }
    else
    if( p->downloads.contains(fileId) )
    {
        FileLocationObject *locObj = p->downloads.take(fileId);
        locObj->download()->setLocation(QString());
        locObj->download()->setFileId(0);
        locObj->download()->setMtime(0);
        locObj->download()->setPartId(0);
        locObj->download()->setTotal(0);
        locObj->download()->setDownloaded(0);
        locObj->download()->file()->close();
        locObj->download()->file()->remove();
    }
}

void TelegramQml::fatalError_slt()
{
    Q_EMIT fatalError();
    try_init();
}

void TelegramQml::insertDialog(const Dialog &d, bool encrypted, bool fromDb)
{
    qint32 did = d.peer().classType()==Peer::typePeerChannel?
                d.peer().channelId() : d.peer().classType()==Peer::typePeerChat?
                    d.peer().chatId() : d.peer().userId();
    DialogObject *obj = p->dialogs.value(did);
    if( !obj )
    {
        obj = new DialogObject(d, this);
        obj->setEncrypted(encrypted);

        p->dialogs.insert(did, obj);

        connect( obj, SIGNAL(unreadCountChanged()), SLOT(refreshUnreadCount()) );
    }
    else
    if(fromDb)
        return;
    else
    {
        *obj = d;
        obj->setEncrypted(encrypted);
    }

    if(d.notifySettings().muteUntil() > 0 && p->globalMute)
        p->userdata->addMute(did);

    p->dialogs_list = p->dialogs.keys();

    telegramp_qml_tmp = p;
    std::stable_sort( p->dialogs_list.begin(), p->dialogs_list.end(), checkDialogLessThan );

    Q_EMIT dialogsChanged(fromDb);

    refreshUnreadCount();

    if(!fromDb)
        p->database->insertDialog(d, encrypted);
}

void TelegramQml::insertMessage(const Message &newMsg, bool encrypted, bool fromDb, bool tempMsg)
{
    if (newMsg.message().isEmpty()
            && newMsg.action().classType() == MessageAction::typeMessageActionEmpty
            && newMsg.media().classType() == MessageMedia::typeMessageMediaEmpty) {
        return;
    }

    Message m = newMsg;
    auto unifiedId = QmlUtils::getUnifiedMessageKey(m.id(), m.toId().channelId());
    auto replyToUnifiedId = QmlUtils::getUnifiedMessageKey(m.replyToMsgId(), m.toId().channelId());

    if(m.replyToMsgId() && !p->messages.contains(replyToUnifiedId))
    {
        if(m.toId().channelId())
        {
            qWarning() << "reading channel reply msg: " << replyToUnifiedId;
            const InputPeer & input = getInputPeer(m.toId().channelId());
            if(requestReadChannelMessage(m.replyToMsgId(), input.channelId(), input.accessHash()))
                p->pending_replies.insert(replyToUnifiedId, unifiedId);
        } else
        {
            if(requestReadMessage(m.replyToMsgId()))
                p->pending_replies.insert(replyToUnifiedId, unifiedId);
        }
        m.setReplyToMsgId(0);
    }

    MessageObject *obj = p->messages.value(unifiedId);
    if( !obj )
    {
        obj = new MessageObject(m, this);
        obj->setEncrypted(encrypted);

        qint32 did = m.toId().chatId();
        if( !did )
            did = m.toId().channelId();
        if( !did )
            did = FLAG_TO_OUT(m.flags())? m.toId().userId() : m.fromId();
        qWarning() << "Inserting message " << m.id() << ", recipient " << did << ", unified id: " << unifiedId;
        p->messages.insert(unifiedId, obj);

        QList<qint64> list = p->messages_list.value(did);

        list << unifiedId;

        telegramp_qml_tmp = p;
        std::stable_sort( list.begin(), list.end(), checkMessageLessThan );

        p->messages_list[did] = list;
    }
    else
    if(fromDb && !encrypted)
        return;
    else
    {
        *obj = m;
        obj->setEncrypted(encrypted);
    }

    Q_EMIT messagesChanged(fromDb && !encrypted);

    if(!fromDb && !tempMsg)
        p->database->insertMessage(m, encrypted);
    if(encrypted)
        updateEncryptedTopMessage(m);


    if(p->pending_replies.contains(unifiedId))
    {
        const QList<qint64> &pends = p->pending_replies.values(unifiedId);
        Q_FOREACH(const qint64 msgId, pends)
        {
            MessageObject *msg = p->messages.value(msgId);
            if(msg)
            {
                msg->setReplyToMsgId(m.id());
                qWarning() << "In message " << msgId << " set reply id to: " << m.id();
            }
        }

        p->pending_replies.remove(unifiedId);
    }
}

void TelegramQml::insertUser(const User &newUser, bool fromDb)
{
    bool become_online = false;
    UserObject *userObj = p->users.value(newUser.id());
    if(!fromDb && userObj && userObj->status()->classType() == UserStatus::typeUserStatusOffline &&
            newUser.status().classType() == UserStatus::typeUserStatusOnline )
        become_online = true;

    if( !userObj )
    {
        userObj = new UserObject(newUser, this);
        p->users.insert(newUser.id(), userObj);

//        getFile(obj->photo()->photoSmall());

        QStringList userNameKeys;
        if(!newUser.username().isEmpty())
        {
            userNameKeys << stringToIndex(newUser.firstName());
            userNameKeys << stringToIndex(newUser.lastName());
            userNameKeys << stringToIndex(newUser.username());
        }

        Q_FOREACH(const QString &key, userNameKeys)
            p->userNameIndexes.insertMulti(key.toLower(), newUser.id());
    }
    else
    if(fromDb)
        return;
    else
        *userObj = newUser;

    if(!fromDb && p->database)
        p->database->insertUser(newUser);

    if(become_online)
        Q_EMIT userBecomeOnline(newUser.id());
    if(newUser.id() == me())
        Q_EMIT myUserChanged();

    Q_EMIT usersChanged();
}

void TelegramQml::insertChat(const Chat &c, bool fromDb)
{
    ChatObject *obj = p->chats.value(c.id());
    if( !obj )
    {
        obj = new ChatObject(c, this);
        p->chats.insert(c.id(), obj);
    }
    else
    if(fromDb)
        return;
    else
        *obj = c;

    if(!fromDb)
        p->database->insertChat(c);

    Q_EMIT chatsChanged();
}

void TelegramQml::insertStickerSet(const StickerSet &set, bool fromDb)
{
    StickerSetObject *obj = p->stickerSets.value(set.id());
    if( !obj )
    {
        obj = new StickerSetObject(set, this);
        p->stickerSets.insert(set.id(), obj);
    }
    else
    if(fromDb)
        return;
    else
        *obj = set;

    Q_EMIT stickersChanged();
}

void TelegramQml::insertStickerPack(const StickerPack &pack, bool fromDb)
{
    StickerPackObject *obj = p->stickerPacks.value(pack.emoticon());
    if( !obj )
    {
        obj = new StickerPackObject(pack, this);
        p->stickerPacks.insert(pack.emoticon(), obj);
    }
    else
    if(fromDb)
        return;
    else
        *obj = pack;
}

void TelegramQml::insertDocument(const Document &doc, bool fromDb)
{
    DocumentObject *obj = p->documents.value(doc.id());
    if( !obj )
    {
        obj = new DocumentObject(doc, this);
        p->documents.insert(doc.id(), obj);
    }
    else
    if(fromDb)
        return;
    else
        *obj = doc;
}

void TelegramQml::insertUpdates(const UpdatesType &updates)
{
    Q_FOREACH( const User & u, updates.users() )
        insertUser(u);
    Q_FOREACH( const Chat & c, updates.chats() )
        insertChat(c);
    Q_FOREACH( const Update & u, updates.updates() )
        insertUpdate(u);

    insertUpdate(updates.update());
    timerUpdateDialogs(500);
}

void TelegramQml::insertUpdate(const Update &update)
{
    UserObject *user = p->users.value(update.userId());
    ChatObject *chat = p->chats.value(update.chatId() ? update.chatId() : update.channelId());

    switch( static_cast<int>(update.classType()) )
    {
    case Update::typeUpdateUserStatus:
        if( user )
        {
            bool become_online = (user->status()->classType() == UserStatus::typeUserStatusOffline &&
                    update.status().classType() == UserStatus::typeUserStatusOnline);

            *(user->status()) = update.status();
            if(become_online)
                Q_EMIT userBecomeOnline(user->id());
        }
        break;

    case Update::typeUpdateNotifySettings:
    {
        NotifyPeer notifyPeer = update.peerNotify();
        PeerNotifySettings settings = update.notifySettings();
        qint32 muteUntil = settings.muteUntil();

        qint32 now = QDateTime::currentDateTime().toTime_t();
        bool isMuted = (muteUntil > now);

        if (notifyPeer.classType() == NotifyPeer::typeNotifyPeer && p->globalMute) {
            Peer peer = notifyPeer.peer();
            qint64 peerId = peer.userId() ? peer.userId() : peer.chatId() ? peer.chatId() : peer.channelId();

            if (isMuted) {
                p->userdata->addMute(peerId);
            } else {
                p->userdata->removeMute(peerId);
            }
        }
    }
        break;

    case Update::typeUpdateMessageID:
    {
        if( !p->pend_messages.contains(update.randomId()) )
            return;

        MessageObject *msgObj = p->pend_messages.take(update.randomId());
        msgObj->setSent(true);

        qint64 old_msgId = msgObj->id();

        Peer peer(static_cast<Peer::PeerClassType>(msgObj->toId()->classType()));
        if (peer.classType() == Peer::typePeerChat)
            peer.setChatId(msgObj->toId()->chatId());
        else if (peer.classType() == Peer::typePeerChannel)
            peer.setChannelId(msgObj->toId()->channelId());
        else
            peer.setUserId(msgObj->toId()->userId());

        Message msg(Message::typeMessage);
        msg.setFromId(msgObj->fromId());
        msg.setId(update.id());
        msg.setDate(msgObj->date());
        msg.setFlags(UNREAD_OUT_TO_FLAG(msgObj->unread(), msgObj->out()));
        msg.setToId(peer);
        msg.setMessage(msgObj->message());
        msg.setReplyToMsgId(msgObj->replyToMsgId());

        qint64 did = msg.toId().chatId();
        if( !did )
            did = msg.toId().channelId();
        if( !did )
            did = FLAG_TO_OUT(msg.flags())? msg.toId().userId() : msg.fromId();

        insertToGarbeges(p->messages.value(old_msgId));
        insertMessage(msg);
        timerUpdateDialogs(3000);
    }
        break;

    case Update::typeUpdateChatUserTyping:
    {
        if(!chat)
            return;
        DialogObject *dlg = p->dialogs.value(chat->id());
        if( !dlg )
            return;
        if( !user )
            return;

        const QString & id_str = QString::number(user->id());
        const QPair<qint64,qint64> & timer_pair = QPair<qint64,qint64>(chat->id(), user->id());
        QStringList tusers = dlg->typingUsers();

        if( tusers.contains(id_str) )
        {
            const int timer_id = p->typing_timers.key(timer_pair);
            killTimer(timer_id);
            p->typing_timers.remove(timer_id);
        }
        else
        {
            tusers << id_str;
            dlg->setTypingUsers( tusers );
            Q_EMIT userStartTyping(user->id(), chat->id());
        }

        int timer_id = startTimer(6000);
        p->typing_timers.insert(timer_id, timer_pair);
    }
        break;

    case Update::typeUpdateEncryption:
        break;

    case Update::typeUpdateUserName:
        if( user )
        {
            user->setFirstName(update.firstName());
            user->setLastName(update.lastName());
        }
        timerUpdateDialogs();
        break;

    case Update::typeUpdateUserBlocked:
        if (update.blocked()) {
            blockUser(update.userId());
        } else {
            unblockUser(update.userId());
        }
        break;

    case Update::typeUpdateNewChannelMessage:
    case Update::typeUpdateNewMessage:
        insertMessage(update.message(), false, false, true);
        timerUpdateDialogs(3000);
        Q_EMIT messagesReceived(1);
        break;

    case Update::typeUpdateContactLink:
        break;

    case Update::typeUpdateChatParticipantDelete:
        if(chat)
            chat->setParticipantsCount( chat->participantsCount()-1 );
        break;

    case Update::typeUpdateNewAuthorization:
        break;

    case Update::typeUpdateChatParticipantAdd:
        if(chat)
            chat->setParticipantsCount( chat->participantsCount()+1 );
        break;

    case Update::typeUpdateDcOptions:
        break;

    case Update::typeUpdateDeleteMessages:
    {
        const QList<qint32> &messages = update.messages();
        Q_FOREACH(quint64 msgId, messages)
        {
            p->database->deleteMessage(msgId);
            insertToGarbeges(p->messages.value(msgId));

            Q_EMIT messagesChanged(false);
        }

        timerUpdateDialogs();
    }
        break;

    case Update::typeUpdateUserTyping:
    {
        if(!user)
            return;
        DialogObject *dlg = p->dialogs.value(user->id());
        if( !dlg )
            return;

        const QString & id_str = QString::number(user->id());
        const QPair<qint64,qint64> & timer_pair = QPair<qint64,qint64>(user->id(), user->id());
        QStringList tusers = dlg->typingUsers();
        if( tusers.contains(id_str) )
        {
            const int timer_id = p->typing_timers.key(timer_pair);
            killTimer(timer_id);
            p->typing_timers.remove(timer_id);
        }
        else
        {
            tusers << id_str;
            dlg->setTypingUsers( tusers );
            Q_EMIT userStartTyping(user->id(), user->id());
        }

        int timer_id = startTimer(6000);
        p->typing_timers.insert(timer_id, timer_pair);
    }
        break;

    case Update::typeUpdateEncryptedChatTyping:
    {
        DialogObject *dlg = p->dialogs.value(update.chat().id());
        if( !dlg )
            return;

        qint64 userId = update.chat().adminId()==me()? update.chat().participantId() : update.chat().adminId();

        const QString & id_str = QString::number(userId);
        const QPair<qint64,qint64> & timer_pair = QPair<qint64,qint64>(userId, userId);
        QStringList tusers = dlg->typingUsers();
        if( tusers.contains(id_str) )
        {
            const int timer_id = p->typing_timers.key(timer_pair);
            killTimer(timer_id);
            p->typing_timers.remove(timer_id);
        }
        else
        {
            tusers << id_str;
            dlg->setTypingUsers( tusers );
            Q_EMIT userStartTyping(userId, userId);
        }

        int timer_id = startTimer(6000);
        p->typing_timers.insert(timer_id, timer_pair);
    }
        break;

    case Update::typeUpdateUserPhoto:
        if( user )
            *(user->photo()) = update.photo();
        timerUpdateDialogs();
        break;

    case Update::typeUpdateContactRegistered:
        timerUpdateDialogs();
        break;

    case Update::typeUpdateNewEncryptedMessage:
        insertEncryptedMessage(update.messageEncrypted());
        break;

    case Update::typeUpdateEncryptedMessagesRead:
    {
        if(!p->messages_list.contains(update.chatId())) {
            qDebug() << __FUNCTION__ << "Trying to update a not existent secret chat";
            return;
        }

        p->database->markMessagesAsReadFromMaxDate(update.chatId(), update.maxDate());
    }
        break;

    case Update::typeUpdateChatParticipants:
        timerUpdateDialogs();
        break;

    case Update::typeUpdateReadChannelInbox:
    case Update::typeUpdateReadHistoryInbox:
    case Update::typeUpdateReadHistoryOutbox:
    {
        const qint64 maxId = update.maxId();
        const qint64 dId = update.peer().channelId()? update.peer().channelId() : update.peer().chatId()? update.peer().chatId() : update.peer().userId();
        const QList<qint64> &msgs = p->messages_list.value(dId);
        Q_FOREACH(qint64 msg, msgs)
            if(msg <= maxId)
            {
                MessageObject *obj = p->messages.value(msg);
                if(obj)
                    obj->setUnread(false);
            }
    }
        break;
    default:

        qWarning() << "Received unhandled updates type: " << update.classType();
        break;
    }
}

void TelegramQml::insertContact(const Contact &c, bool fromDb)
{
    ContactObject *obj = p->contacts.value(c.userId());
    if( !obj )
    {
        obj = new ContactObject(c, this);
        p->contacts.insert(c.userId(), obj);
    }
    else
        *obj = c;

    if(!fromDb)
        p->database->insertContact(c);

    Q_EMIT contactsChanged();
}

void TelegramQml::insertEncryptedMessage(const EncryptedMessage &e)
{
    EncryptedMessageObject *obj = p->encmessages.value(e.randomId()); //NOTE: There was e.file().id there
    if( !obj )
    {
        obj = new EncryptedMessageObject(e, this);
        p->encmessages.insert(e.randomId(), obj);
    }
    else
        *obj = e;

    Q_EMIT incomingEncryptedMessage(obj);
}

void TelegramQml::insertEncryptedChat(const EncryptedChat &c)
{
    EncryptedChatObject *obj = p->encchats.value(c.id());
    if( !obj )
    {
        obj = new EncryptedChatObject(c, this);
        p->encchats.insert(c.id(), obj);
    }
    else
        *obj = c;

    Q_EMIT encryptedChatsChanged();

    Peer peer(Peer::typePeerUser);
    peer.setUserId(c.id());

    Dialog dlg;
    dlg.setPeer(peer);

    DialogObject *dobj = p->dialogs.value(c.id());
    if(dobj)
        dlg.setTopMessage(dobj->topMessage());

    insertDialog(dlg, true);
}

void TelegramQml::insertSecretChatMessage(const SecretChatMessage &sc, bool cachedMsg)
{
    const qint32 chatId = sc.chatId();
    const DecryptedMessage &m = sc.decryptedMessage();
    const qint32 date = sc.date();
    const EncryptedFile &attachment = sc.attachment();

    EncryptedChatObject *chat = p->encchats.value(chatId);
    if(!chat)
        return;

    MessageAction action;
    const DecryptedMessageMedia &dmedia = m.media();
    const DecryptedMessageAction &daction = m.action();
    if(m.message().isEmpty() && dmedia.classType()==DecryptedMessageMedia::typeDecryptedMessageMediaEmptySecret8)
    {
        switch(static_cast<int>(daction.classType()))
        {
        case DecryptedMessageAction::typeDecryptedMessageActionNotifyLayerSecret17:
            action.setClassType(MessageAction::typeMessageActionChatCreate);
            action.setUserId(chat->adminId());
            action.setUsers(QList<qint32>()<<chat->adminId());
            action.setTitle( tr("Secret Chat") );
            break;
        }
    }

    Peer peer(Peer::typePeerChat);
    peer.setChatId(chatId);

    Message msg(Message::typeMessage);
    msg.setToId(peer);
    msg.setMessage(m.message());
    msg.setDate(date);
    msg.setId(date);
    msg.setAction(action);
    msg.setFromId(chat->adminId()==me()?chat->participantId():chat->adminId());

    bool out = (msg.fromId()==me());
    msg.setFlags(UNREAD_OUT_TO_FLAG(false,out));

    bool hasMedia = (dmedia.classType() != DecryptedMessageMedia::typeDecryptedMessageMediaEmptySecret8);
    bool hasInternalMedia = false;
    if(hasMedia)
    {
        MessageMedia media;
        if(dmedia.classType() == DecryptedMessageMedia::typeDecryptedMessageMediaExternalDocumentSecret23)
        {
            Document doc(Document::typeDocument);
            doc.setAccessHash(dmedia.accessHash());
            doc.setSize(dmedia.size());
            doc.setDcId(dmedia.dcId());
            doc.setId(dmedia.id());
            doc.setMimeType(dmedia.mimeType());
            doc.setThumb( insertCachedPhotoSize(dmedia.thumbPhotoSize()) );
            doc.setDate(dmedia.date());
            doc.setAttributes(dmedia.attributes());

            media.setDocument(doc);
            media.setClassType(MessageMedia::typeMessageMediaDocument);
        }
        else
        if(dmedia.classType() == DecryptedMessageMedia::typeDecryptedMessageMediaPhotoSecret8)
        {
            QList<PhotoSize> photoSizes;

            PhotoSize psize(PhotoSize::typePhotoSize);
            psize.setSize(dmedia.size());
            psize.setW(dmedia.w());
            psize.setH(dmedia.h());
            photoSizes << psize;

            PhotoSize thumbSize(PhotoSize::typePhotoSize);
            if(dmedia.thumbPhotoSize().classType() != PhotoSize::typePhotoSizeEmpty)
            {
                thumbSize = dmedia.thumbPhotoSize();
                thumbSize.setW(dmedia.w());
                thumbSize.setH(dmedia.h());
            }
            else
            {
                thumbSize.setClassType(PhotoSize::typePhotoCachedSize);
                thumbSize.setBytes(dmedia.thumbBytes());
                thumbSize.setW(dmedia.w()/2);
                thumbSize.setH(dmedia.h()/2);

                thumbSize = insertCachedPhotoSize(thumbSize);
            }

            if(thumbSize.classType() == PhotoSize::typePhotoSize)
                photoSizes << thumbSize;

            Photo photo(Photo::typePhoto);
            photo.setId(attachment.id());
            photo.setAccessHash(attachment.accessHash());
            photo.setDate(msg.date());
            photo.setSizes(QList<PhotoSize>()<<photoSizes);

            media.setPhoto(photo);
            media.setClassType(MessageMedia::typeMessageMediaPhoto);

            hasInternalMedia = true;
        }
        else
        if(dmedia.classType() == DecryptedMessageMedia::typeDecryptedMessageMediaVideoSecret17 ||
           dmedia.classType() == DecryptedMessageMedia::typeDecryptedMessageMediaVideoSecret8)
        {
            Video video(Video::typeVideo);
            video.setId(attachment.id());
            video.setDcId(attachment.dcId());
            video.setAccessHash(attachment.accessHash());
            video.setDate(msg.date());
            video.setSize(dmedia.size());
            video.setH(dmedia.h());
            video.setW(dmedia.w());
            video.setDuration(dmedia.duration());

            if(dmedia.classType() == DecryptedMessageMedia::typeDecryptedMessageMediaVideoSecret8)
            {
                PhotoSize thumbSize(PhotoSize::typePhotoCachedSize);
                thumbSize.setW(dmedia.w());
                thumbSize.setH(dmedia.h());
                video.setThumb( insertCachedPhotoSize(thumbSize) );
            }

            media.setVideo(video);
            media.setClassType(MessageMedia::typeMessageMediaVideo);

            hasInternalMedia = true;
        }
        else
        if(dmedia.classType() == DecryptedMessageMedia::typeDecryptedMessageMediaAudioSecret17 ||
           dmedia.classType() == DecryptedMessageMedia::typeDecryptedMessageMediaAudioSecret8)
        {
            Audio audio(Audio::typeAudio);
            audio.setId(attachment.id());
            audio.setDcId(attachment.dcId());
            audio.setAccessHash(attachment.accessHash());
            audio.setDate(msg.date());
            audio.setSize(dmedia.size());
            audio.setDuration(dmedia.duration());

            media.setAudio(audio);
            media.setClassType(MessageMedia::typeMessageMediaAudio);

            hasInternalMedia = true;
        }
        else
        if(dmedia.classType() == DecryptedMessageMedia::typeDecryptedMessageMediaVideoSecret17 ||
           dmedia.classType() == DecryptedMessageMedia::typeDecryptedMessageMediaVideoSecret8)
        {
            Document doc(Document::typeDocument);
            doc.setAccessHash(attachment.accessHash());
            doc.setId(attachment.id());
            doc.setDcId(attachment.dcId());
            doc.setSize(attachment.size());

            media.setDocument(doc);
            media.setClassType(MessageMedia::typeMessageMediaDocument);

            hasInternalMedia = true;
        }

        msg.setMedia(media);
    }

    insertMessage(msg, true);

    auto unifiedId = QmlUtils::getUnifiedMessageKey(msg.id(), msg.toId().channelId());
    MessageObject *msgObj = p->messages.value(unifiedId);
    if(msgObj && hasInternalMedia)
    {
        msgObj->media()->setEncryptKey(dmedia.key());
        msgObj->media()->setEncryptIv(dmedia.iv());

        p->database->insertMediaEncryptedKeys(unifiedId, dmedia.key(), dmedia.iv());
    }

    if(!FLAG_TO_OUT(msg.flags()) && !cachedMsg)
        Q_EMIT incomingMessage(msgObj);

    Peer dlgPeer(Peer::typePeerUser);
    dlgPeer.setUserId(chatId);

    Dialog dlg;
    dlg.setPeer(dlgPeer);

    dlg.setTopMessage(msg.id());

    if(!FLAG_TO_OUT(msg.flags()) && sc.decryptedMessage().action().classType() != DecryptedMessageAction::typeDecryptedMessageActionNotifyLayerSecret17)
    {
        DialogObject *dobj = p->dialogs.value(chatId);
        dlg.setUnreadCount( dobj? dobj->unreadCount()+1 : 1 );
    }

    insertDialog(dlg, true);
}

PhotoSize TelegramQml::insertCachedPhotoSize(const PhotoSize &photo)
{
    PhotoSize result(PhotoSize::typePhotoSize);
    if(photo.classType() != PhotoSize::typePhotoCachedSize || photo.bytes().isEmpty())
        return photo;

    FileLocation location(FileLocation::typeFileLocation);
    location.setVolumeId(generateRandomId());
    location.setLocalId(generateRandomId());
    location.setSecret(generateRandomId());
    location.setDcId(0);

    FileLocationObject locObj(location.classType());
    locObj.setVolumeId(location.volumeId());
    locObj.setLocalId(location.localId());
    locObj.setSecret(location.secret());
    locObj.setDcId(location.dcId());

    const QString &path = fileLocation(&locObj);
    QFile file(path);
    if(!file.open(QFile::WriteOnly))
        return photo;

    file.write(photo.bytes());
    file.close();

    result.setH(photo.h());
    result.setW(photo.w());
    result.setSize(photo.bytes().length());
    result.setLocation(location);

    return result;
}

void TelegramQml::blockUser(qint64 userId)
{
    Q_EMIT userBlocked(userId);
    p->database->blockUser(userId);
}

void TelegramQml::unblockUser(qint64 userId)
{
    Q_EMIT userUnblocked(userId);
    p->database->unblockUser(userId);
}

void TelegramQml::deleteLocalHistory(qint64 peerId) {
    bool isEncrypted = p->encchats.contains(peerId);
    bool deleteDialog = p->deleteChatIds.contains(peerId);
    if (deleteDialog) {
        p->deleteChatIds.remove(peerId);
    }

    DialogObject *dialog = p->dialogs.value(peerId);
    if (dialog) {
        dialog->setTopMessage(0);
        dialog->setUnreadCount(0);
    }

    p->database->deleteHistory(peerId);
    const QList<qint64> & messages = p->messages_list.value(peerId);
    if (isEncrypted) {
        Q_FOREACH(qint64 msgId, messages) {
            insertToGarbeges(p->encmessages.value(msgId));
        }
    } else {
        Q_FOREACH(qint64 msgId, messages) {
            insertToGarbeges(p->messages.value(msgId));
        }
    }
    Q_EMIT messagesChanged(false);

    if (deleteDialog) {
        p->database->deleteDialog(peerId);
        insertToGarbeges(p->chats.value(peerId));
        insertToGarbeges(p->encchats.value(peerId));
        insertToGarbeges(p->dialogs.value(peerId));
        Q_EMIT dialogsChanged(false);
    }

    timerUpdateDialogs(3000);
}

void TelegramQml::timerEvent(QTimerEvent *e)
{
    if( e->timerId() == p->upd_dialogs_timer )
    {
        if( p->telegram && p->telegram->isConnected() )
        {
            p->telegram->messagesGetDialogs(0, 1000);
            p->telegram->channelsGetDialogs(0, 1000);
        }

        killTimer(p->upd_dialogs_timer);
        p->upd_dialogs_timer = 0;
    }
    else
    if ( e->timerId() == p->update_contacts_timer)
    {
        if ( p->telegram )
            p->telegram->contactsGetContacts();

        killTimer(p->update_contacts_timer);
        p->update_contacts_timer = 0;
    } else
    if( e->timerId() == p->garbage_checker_timer )
    {
        Q_FOREACH( QObject *obj, p->garbages )
            obj->deleteLater();

        p->garbages.clear();
        killTimer(p->garbage_checker_timer);
        p->garbage_checker_timer = 0;
    }
    else
    if( p->typing_timers.contains(e->timerId()) )
    {
        killTimer(e->timerId());

        const QPair<qint64,qint64> & pair = p->typing_timers.take(e->timerId());
        DialogObject *dlg = p->dialogs.value(pair.first);
        if( !dlg )
            return;

        QStringList typings = dlg->typingUsers();
        typings.removeAll(QString::number(pair.second));

        dlg->setTypingUsers(typings);
    }
}

void TelegramQml::startGarbageChecker()
{
    if( p->garbage_checker_timer )
        killTimer(p->garbage_checker_timer);

    p->garbage_checker_timer = startTimer(3000);
}

void TelegramQml::insertToGarbeges(QObject *obj)
{
    if (obj == NULL) return;

    if(qobject_cast<MessageObject*>(obj))
    {
        MessageObject *msg = qobject_cast<MessageObject*>(obj);
        const qint64 mId = msg->unifiedId();
        const qint64 dId = messageDialogId(mId);

        p->messages_list[dId].removeAll(mId);
        p->messages.remove(mId);
        p->uploads.remove(mId);
        p->pend_messages.remove(mId);
    }
    else
    if(qobject_cast<DialogObject*>(obj))
    {
        DialogObject *dlg = qobject_cast<DialogObject*>(obj);

        qint64 dId;
        if (dlg->peer()->classType()==Peer::typePeerChat)
            dId = dlg->peer()->chatId();
        else if (dlg->peer()->classType()==Peer::typePeerChannel)
            dId = dlg->peer()->channelId();
        else
            dId = dlg->peer()->userId();

        p->dialogs.remove(dId);
        p->fakeDialogs.remove(dId);
        p->dialogs_list.removeAll(dId);
    }
    else
    if(qobject_cast<ChatObject*>(obj))
    {
        ChatObject *chat = qobject_cast<ChatObject*>(obj);
        const qint32 chatId = chat->id();

        p->chats.remove(chatId);
    }
    else
    if(qobject_cast<UserObject*>(obj))
    {
        UserObject *user = qobject_cast<UserObject*>(obj);
        const qint32 userId = user->id();

        p->users.remove(userId);
    }

    p->garbages.insert(obj);
    startGarbageChecker();
}

void TelegramQml::dbUserFounded(const User &user)
{
    insertUser(user, true);
}

void TelegramQml::dbChatFounded(const Chat &chat)
{
    insertChat(chat, true);
}

void TelegramQml::dbDialogFounded(const Dialog &dialog, bool encrypted)
{
    insertDialog(dialog, encrypted, true);

    if(encrypted && p->tsettings)
    {
        const QList<SecretChat*> &secrets = p->tsettings->secretChats();
        Q_FOREACH(SecretChat *sc, secrets)
        {
            if(sc->chatId() != dialog.peer().userId())
                continue;

            EncryptedChat chat(EncryptedChat::typeEncryptedChat);
            chat.setAccessHash(sc->accessHash());
            chat.setAdminId(sc->adminId());
            chat.setDate(sc->date());
            chat.setGAOrB(sc->gAOrB());
            chat.setId(sc->chatId());
            chat.setKeyFingerprint(sc->keyFingerprint());
            chat.setParticipantId(sc->participantId());

            insertEncryptedChat(chat);
        }
    }
}

void TelegramQml::dbContactFounded(const Contact &contact)
{
    insertContact(contact, true);
}

void TelegramQml::dbMessageFounded(const Message &message)
{
    bool encrypted = false;
    DialogObject *dlg = p->dialogs.value(message.toId().chatId());
    if(dlg)
        encrypted = dlg->encrypted();

    insertMessage(message, encrypted, true);
}

void TelegramQml::dbMediaKeysFounded(qint64 mediaId, const QByteArray &key, const QByteArray &iv)
{
    MessageObject *msg = p->messages.value(mediaId);
    if(!msg)
        return;

    msg->media()->setEncryptKey(key);
    msg->media()->setEncryptIv(iv);
}

void TelegramQml::refreshUnreadCount()
{
    int unreadCount = 0;
    Q_FOREACH( DialogObject *obj, p->dialogs )
    {
        qint64 dId;
        if (obj->peer()->classType()==Peer::typePeerChat)
            dId = obj->peer()->chatId();
        else if (obj->peer()->classType()==Peer::typePeerChannel)
            dId = obj->peer()->channelId();
        else
            dId = obj->peer()->userId();
        if(p->userdata && (p->userdata->notify(dId) & UserData::DisableBadges) )
            continue;

        unreadCount += obj->unreadCount();
    }

    if( p->unreadCount == unreadCount )
        return;

    p->unreadCount = unreadCount;
    Q_EMIT unreadCountChanged();
}

void TelegramQml::refreshTotalUploadedPercent()
{
    qint64 totalSize = 0;
    qint64 uploaded = 0;
    Q_FOREACH(UploadObject *upld, p->uploadPercents)
    {
        totalSize += upld->totalSize();
        uploaded += upld->uploaded();
    }

    if(totalSize == 0)
        p->totalUploadedPercent = 0;
    else
        p->totalUploadedPercent = 100.0*uploaded/totalSize;

    Q_EMIT totalUploadedPercentChanged();

    if(p->totalUploadedPercent == 100)
    {
        p->totalUploadedPercent = 0;
        p->uploadPercents.clear();
        Q_EMIT totalUploadedPercentChanged();
    }
}

void TelegramQml::refreshSecretChats()
{
    if(!p->tsettings)
        return;

    const QList<SecretChat*> &secrets = p->tsettings->secretChats();
    Q_FOREACH(SecretChat *sc, secrets)
    {

        EncryptedChat chat(EncryptedChat::typeEncryptedChat);

        if (sc->date() == 0)
            chat.setClassType(EncryptedChat::typeEncryptedChatWaiting);

        chat.setAccessHash(sc->accessHash());
        chat.setAdminId(sc->adminId());
        chat.setDate(sc->date());
        chat.setGAOrB(sc->gAOrB());
        chat.setId(sc->chatId());
        chat.setKeyFingerprint(sc->keyFingerprint());
        chat.setParticipantId(sc->participantId());

        insertEncryptedChat(chat);
    }
}

void TelegramQml::updateEncryptedTopMessage(const Message &message)
{
    qint64 dId = message.toId().chatId();
    if(!dId)
        return;

    DialogObject *dlg = p->dialogs.value(dId);
    if(!dlg)
        return;

    MessageObject *topMessage = p->messages.value(dlg->topMessage());
    if(dlg->topMessage() && !topMessage)
        return;

    qint32 topMsgDate = topMessage? topMessage->date() : 0;
    if(message.date() < topMsgDate)
        return;

    Peer peer(Peer::typePeerUser);
    peer.setUserId(dlg->peer()->userId());

    Dialog dialog;
    //FIXME: Why the date is set as top Message???
    dialog.setTopMessage(message.id());
    dialog.setUnreadCount(dlg->unreadCount());
    dialog.setPeer(peer);

    insertDialog(dialog, true, false);
}

void TelegramQml::getMyUser()
{
    if(!p->telegram)
        return;

    InputUser user(InputUser::typeInputUserSelf);
    user.setUserId(me());

    p->telegram->usersGetUsers(QList<InputUser>()<<user);
}

qint64 TelegramQml::generateRandomId() const
{
    qint64 randomId;
    Utils::randomBytes(&randomId, 8);
    return randomId;
}

Peer::PeerClassType TelegramQml::getPeerType(qint64 pid)
{
    Peer::PeerClassType res;

    if(p->users.contains(pid))
        res = Peer::typePeerUser;
    else if(p->chats.contains(pid))
    {
        ChatObject *chat = p->chats.value(pid);
        if (chat->classType() == Chat::typeChannel ||
            chat->classType() == Chat::typeChannelForbidden)
            res = Peer::typePeerChannel;
        else
            res = Peer::typePeerChat;
    }

    return res;
}

InputPeer::InputPeerClassType TelegramQml::getInputPeerType(qint64 pid)
{
    InputPeer::InputPeerClassType res = InputPeer::typeInputPeerEmpty;

    if(p->users.contains(pid))
    {
        UserObject *user = p->users.value(pid);
        switch(user->classType())
        {
            case User::typeUser:
                res = InputPeer::typeInputPeerUser;
            break;
        }
    }
    else if(p->chats.contains(pid))
    {
        ChatObject *chat = p->chats.value(pid);
        if (chat->classType() == Chat::typeChannel ||
            chat->classType() == Chat::typeChannelForbidden)
            res = InputPeer::typeInputPeerChannel;
        else
            res = InputPeer::typeInputPeerChat;
    }

    return res;
}

QStringList TelegramQml::stringToIndex(const QString &str)
{
    return QStringList() << str.toLower();
}

void TelegramQml::objectDestroyed(QObject *obj)
{
    if(qobject_cast<UploadObject*>(obj))
    {
        p->uploadPercents.remove( static_cast<UploadObject*>(obj) );
        refreshTotalUploadedPercent();
    }
    if(qobject_cast<FileLocationObject*>(obj))
    {
        p->accessHashes.remove( static_cast<FileLocationObject>(obj).accessHash() );
    }
}

TelegramQml::~TelegramQml()
{
    if( p->telegram )
        delete p->telegram;

    delete p;
}

bool checkDialogLessThan( qint64 a, qint64 b )
{
    DialogObject *ao = telegramp_qml_tmp->dialogs.value(a);
    DialogObject *bo = telegramp_qml_tmp->dialogs.value(b);
    if( !ao )
        return false;
    if( !bo )
        return true;

    MessageObject *am = telegramp_qml_tmp->messages.value(QmlUtils::getUnifiedMessageKey(ao->topMessage(), ao->peer()->channelId()));
    MessageObject *bm = telegramp_qml_tmp->messages.value(QmlUtils::getUnifiedMessageKey(bo->topMessage(), bo->peer()->channelId()));
    if(!am || !bm)
    {
        EncryptedChatObject *aec = telegramp_qml_tmp->encchats.value(a);
        EncryptedChatObject *bec = telegramp_qml_tmp->encchats.value(b);
        if(aec && bm)
            return aec->date() > bm->date();
        else
        if(am && bec)
            return am->date() > bec->date();
        else
        if(aec && bec)
            return aec->date() > bec->date();
        else
            return ao->topMessage() > bo->topMessage();
    }

    return am->date() > bm->date();
}

bool checkMessageLessThan( qint64 a, qint64 b )
{
    MessageObject *am = telegramp_qml_tmp->messages.value(a);
    MessageObject *bm = telegramp_qml_tmp->messages.value(b);
    if(am && bm)
        return am->date() > bm->date();
    else
        return a > b;
}
