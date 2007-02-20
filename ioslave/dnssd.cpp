/***************************************************************************
 *   Copyright (C) 2004, 2005 by Jakub Stachowski                          *
 *   qbast@go2.pl                                                          *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.         *
 ***************************************************************************/

#include <q3cstring.h>
#include <q3socket.h>
#include <qdatetime.h>
#include <qbitarray.h>

#include <stdlib.h>
#include <math.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <netdb.h>

#include <kconfig.h>
#include <kdebug.h>
#include <kmessagebox.h>
#include <kcomponentdata.h>
#include <kglobal.h>
#include <kstandarddirs.h>
#include <ksocketaddress.h>
#include <kprotocolinfo.h>
#include <kcmdlineargs.h>
#include <klocale.h>
#include <kurl.h>
#include <qmap.h>
#include <kapplication.h>
#include <qeventloop.h>
#include <dnssd/domainbrowser.h>
#include <krun.h>
#include <kprotocolmanager.h>

#include "dnssd.h"

static const KCmdLineOptions options[] =
{
	{ "+protocol", I18N_NOOP( "Protocol name" ), 0 },
	{ "+pool", I18N_NOOP( "Socket name" ), 0 },
	{ "+app", I18N_NOOP( "Socket name" ), 0 },
	KCmdLineLastOption
};

ZeroConfProtocol::ZeroConfProtocol(const QByteArray& protocol, const QByteArray &pool_socket, const QByteArray &app_socket)
		: SlaveBase(protocol, pool_socket, app_socket), browser(0),toResolve(0),
		configData(0)
{}


void ZeroConfProtocol::get(const KUrl& url )
{
	if (!dnssdOK()) return;
	UrlType t = checkURL(url);
	switch (t) {
	case HelperProtocol:
	{
		resolveAndRedirect(url,true);
		mimeType("text/html");
		QString reply= "<html><head><meta http-equiv=\"Content-Type\" content=\"text/html; charset=utf-8\">\n";
		reply+="</head>\n<body>\n<h2>"+i18n("Requested service has been launched in separate window.");
		reply+="</h2>\n</body></html>";
		data(reply.toUtf8());
		data(QByteArray());
		finished();
		break;
	}
	case Service:
		resolveAndRedirect(url);
		break;
	default:
		error(ERR_MALFORMED_URL,i18n("invalid URL"));
	}
}
void ZeroConfProtocol::mimetype(const KUrl& url )
{
	resolveAndRedirect(url);
}

UrlType ZeroConfProtocol::checkURL(const KUrl& url)
{
	if (url.path()=="/") return RootDir;
	QString service, type, domain;
	dissect(url,service,type,domain);
	const QString& proto = type.section('.',1,-1);
	if (type[0]!='_' || (proto!="_udp" && proto!="_tcp")) return Invalid;
	if (service.isEmpty()) return ServiceDir;
	if (!domain.isEmpty()) {
		if (!setConfig(type)) return Invalid;
		if (!configData->readEntry("Exec").isNull()) return HelperProtocol;
		return (KProtocolInfo::isHelperProtocol( configData->readEntry( "Protocol",
			type.section(".",0,0).mid(1)))) ? HelperProtocol : Service;
		}
	return Invalid;
}

// URL zeroconf://domain/_http._tcp/some%20service
// URL invitation://host:port/_http._tcp/some%20service?u=username&root=directory
void ZeroConfProtocol::dissect(const KUrl& url,QString& name,QString& type,QString& domain)
{
	type = url.path().section("/",1,1);
	domain = url.host();
	name = url.path().section("/",2,-1);

}

bool ZeroConfProtocol::dnssdOK()
{
	switch(ServiceBrowser::isAvailable()) {
        	case ServiceBrowser::Stopped:
			error(KIO::ERR_UNSUPPORTED_ACTION,
			i18n("The Zeroconf daemon (mdnsd) is not running."));
			return false;
		case ServiceBrowser::Unsupported:
	    		error(KIO::ERR_UNSUPPORTED_ACTION,
			i18n("KDE has been built without Zeroconf support."));
			return false;
          default:
			return true;
        }
}

void ZeroConfProtocol::stat(const KUrl& url)
{
	UDSEntry entry;
	if (!dnssdOK()) return;
	UrlType t = checkURL(url);
	switch (t) {
	case RootDir:
	case ServiceDir:
		buildDirEntry(entry,"");
		statEntry(entry);
		finished();
		break;
	case Service:
		resolveAndRedirect(url);
	  	break;
	case HelperProtocol:
	{
		QString name,type,domain;
		dissect(url,name,type,domain);
		buildServiceEntry(entry,name,type,domain);
		statEntry(entry);
		finished();
		break;
	}
	default:
		error(ERR_MALFORMED_URL,i18n("invalid URL"));
	}
}
QString ZeroConfProtocol::getAttribute(const QString& name)
{
	QString entry = configData->readEntry(name,QString());
	return (entry.isNull()) ? QString() : toResolve->textData()[entry];
}

void ZeroConfProtocol::resolveAndRedirect(const KUrl& url, bool useKRun)
{
	QString name,type,domain;
	dissect(url,name,type,domain);
	if (url.protocol()=="invitation") {
		delete toResolve;
		toResolve=0;
		toResolve= new RemoteService(url);
		if (!toResolve->isResolved()) error(ERR_MALFORMED_URL,i18n("Invalid URL"));
	} else {
		kDebug() << "Resolve for  " << name << ", " << type << ", " << domain  << "\n";
		if (toResolve!=0)
			if (toResolve->serviceName()==name && toResolve->type()==type &&
			        toResolve->domain()==domain && toResolve->isResolved()) {
			}  else {
				delete toResolve;
				toResolve = 0;
			}
		if (toResolve==0) {
			toResolve = new RemoteService(name,type,domain);
			// or maybe HOST_NOT_FOUND?
			if (!toResolve->resolve()) error(ERR_SERVICE_NOT_AVAILABLE,i18n("Unable to resolve service"));
		}
	}
	KUrl destUrl;
	kDebug() << "Resolved: " << toResolve->hostName() << "\n";
	destUrl.setProtocol(getProtocol(type));
	destUrl.setUser(getAttribute("UserEntry"));
	destUrl.setPass(getAttribute("PasswordEntry"));
	destUrl.setPath(getAttribute("PathEntry"));
	destUrl.setHost(toResolve->hostName());
	destUrl.setPort(toResolve->port());
	// get exec from config or try getting it from helper protocol
	if (useKRun) KRun::run(configData->readEntry("Exec",KProtocolInfo::exec(getProtocol(type))),destUrl);
	else {
		redirection(destUrl);
		finished();
	}
}

bool ZeroConfProtocol::setConfig(const QString& type)
{
	kDebug() << "Setting config for " << type << endl;
	if (configData)
		if (configData->readEntry("Type")!=type) delete configData;
		else return true;
	configData = new KConfig("data", "zeroconf/"+type, KConfig::NoGlobals );
	return (configData->readEntry("Type")==type);
}


void ZeroConfProtocol::buildDirEntry(UDSEntry& entry,const QString& name,const QString& type, const QString& host)
{
	entry.clear();
	entry.insert(UDS_NAME,name);
	entry.insert(UDS_ACCESS,0555);
	entry.insert(UDS_SIZE,0);
	entry.insert(UDS_FILE_TYPE,S_IFDIR);
	entry.insert(UDS_MIME_TYPE,QString::fromUtf8("inode/directory"));
	if (!type.isNull())
			entry.insert(UDS_URL,"zeroconf:/"+((!host.isNull()) ? '/'+host+'/' : "" )+type+'/');
}
QString ZeroConfProtocol::getProtocol(const QString& type)
{
	setConfig(type);
	return configData->readEntry("Protocol",type.section(".",0,0).mid(1));
}

void ZeroConfProtocol::buildServiceEntry(UDSEntry& entry,const QString& name,const QString& type,const QString& domain)
{
	setConfig(type);
	entry.clear();
	entry.insert(UDS_NAME,name);
	entry.insert(UDS_ACCESS,0666);
	QString icon=configData->readEntry("Icon",KProtocolInfo::icon(getProtocol(type)));
	if (!icon.isNull())
			entry.insert(UDS_ICON_NAME,icon);
	KUrl protourl;
	protourl.setProtocol(getProtocol(type));
	QString encname = "zeroconf://" + domain +"/" +type+ "/" + name;
	if (KProtocolManager::supportsListing(protourl)) {
		entry.insert(UDS_FILE_TYPE,S_IFDIR);
		encname+='/';
	} else
			entry.insert(UDS_FILE_TYPE,S_IFREG);
	entry.insert(UDS_URL,encname);
}

void ZeroConfProtocol::listDir(const KUrl& url )
{

	if (!dnssdOK()) return;
	UrlType t  = checkURL(url);
	UDSEntry entry;
	switch (t) {
	case RootDir:
		if (allDomains=url.host().isEmpty())
			browser = new ServiceBrowser(ServiceBrowser::AllServices);
			else browser = new ServiceBrowser(ServiceBrowser::AllServices,url.host());
		connect(browser,SIGNAL(serviceAdded(DNSSD::RemoteService::Ptr)),
			this,SLOT(newType(DNSSD::RemoteService::Ptr)));
		break;
	case ServiceDir:
		if (url.host().isEmpty())
			browser = new ServiceBrowser(url.path(KUrl::RemoveTrailingSlash).section("/",1,-1));
			else browser = new ServiceBrowser(url.path(KUrl::RemoveTrailingSlash).section("/",1,-1),url.host());
		connect(browser,SIGNAL(serviceAdded(DNSSD::RemoteService::Ptr)),
			this,SLOT(newService(DNSSD::RemoteService::Ptr)));
		break;
	case Service:
		resolveAndRedirect(url);
	  	return;
	default:
		error(ERR_MALFORMED_URL,i18n("invalid URL"));
		return;
	}
	connect(browser,SIGNAL(finished()),this,SLOT(allReported()));
	browser->startBrowse();
	enterLoop();
}
void ZeroConfProtocol::allReported()
{
	UDSEntry entry;
	listEntry(entry,true);
	finished();
	delete browser;
	browser=0;
	mergedtypes.clear();
	emit leaveModality();
}
void ZeroConfProtocol::newType(DNSSD::RemoteService::Ptr srv)
{
	if (mergedtypes.contains(srv->type())>0) return;
	mergedtypes << srv->type();
	UDSEntry entry;
	kDebug() << "Got new entry " << srv->type() << endl;
	if (!setConfig(srv->type())) return;
	QString name = configData->readEntry("Name");
	if (!name.isNull()) {
		buildDirEntry(entry,name,srv->type(), (allDomains) ? QString::null :
			browser->browsedDomains()->domains()[0]);
		listEntry(entry,false);
	}
}
void ZeroConfProtocol::newService(DNSSD::RemoteService::Ptr srv)
{
	UDSEntry entry;
	buildServiceEntry(entry,srv->serviceName(),srv->type(),srv->domain());
	listEntry(entry,false);
}
void ZeroConfProtocol::enterLoop()
{
	QEventLoop eventLoop;
	connect(this, SIGNAL(leaveModality()),&eventLoop, SLOT(quit()));
	eventLoop.exec(QEventLoop::ExcludeUserInputEvents);
}

extern "C"
{
	int KDE_EXPORT kdemain( int argc, char **argv )
	{
		// KApplication is necessary to use other ioslaves
		putenv(strdup("SESSION_MANAGER="));
		KCmdLineArgs::init(argc, argv, "kio_zeroconf", 0, 0, 0, 0);
		KCmdLineArgs::addCmdLineOptions( options );
		//KApplication::disableAutoDcopRegistration();
		KApplication app;
		KCmdLineArgs *args = KCmdLineArgs::parsedArgs();
		ZeroConfProtocol slave( args->arg(0), args->arg(1), args->arg(2) );
		slave.dispatchLoop();
		return 0;
	}
}


#include "dnssd.moc"

