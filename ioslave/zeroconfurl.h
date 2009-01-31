/***************************************************************************
 *   Copyright 2009 Friedrich W. H. Kossebau                               *
 *   kossebau@kde.org                                                      *
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

#ifndef ZEROCONFURL_H
#define ZEROCONFURL_H

// KDE
#include <DNSSD/RemoteService>
#include <KUrl>


// URL zeroconf:/_http._tcp/some%20service
class ZeroConfUrl
{
  public:
    enum Type { InvalidUrl, RootDir, ServiceDir, Service };

  public:
    static QString createUrl( const DNSSD::RemoteService::Ptr& remoteService );

  public:
    explicit ZeroConfUrl( const KUrl& url );

  public:
    const QString& serviceType() const;
    const QString& serviceName() const;
    bool matches( const RemoteService* remoteService ) const;
    ZeroConfUrl::Type type() const;

  private:
    QString mServiceType;
    QString mServiceName;
};


inline ZeroConfUrl::ZeroConfUrl( const KUrl& url )
{
    // FIXME: encode domain name into url to support many services with the same name on 
    // different domains
    mServiceType = url.path().section('/',1,1);
    mServiceName = url.path().section('/',2,-1);
}

inline const QString& ZeroConfUrl::serviceType() const { return mServiceType; }
inline const QString& ZeroConfUrl::serviceName() const { return mServiceName; }

inline bool ZeroConfUrl::matches( const RemoteService* remoteService ) const
{
    return ( remoteService->serviceName()==mServiceName && remoteService->type()==mServiceType );
}

// TODO: how is a invalid url defined?
inline ZeroConfUrl::Type ZeroConfUrl::type() const
{
    Type result =
        mServiceType.isEmpty() ? RootDir :
        mServiceName.isEmpty() ? ServiceDir :
        /*else*/                 Service;

    return result;
}

inline QString ZeroConfUrl::createUrl( const DNSSD::RemoteService::Ptr& remoteService )
{
    return "zeroconf:/" + remoteService->type() + '/' + remoteService->serviceName();
}

#endif
