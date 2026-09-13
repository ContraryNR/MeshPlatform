#ifndef TUNMANAGER_H
#define TUNMANAGER_H
#include <QProcess>
#include <QObject>
#include "tunloader.h"
#include <windows.h>
#include <iphlpapi.h>
#include <netioapi.h>
#include <ws2tcpip.h>
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")

class tunmanager : public QObject
{
    Q_OBJECT

public:
    tunloader* functionLoader{NULL};
    tunmanager(tunloader* loader):functionLoader(loader){}
public:
    WINTUN_ADAPTER_HANDLE initialAdapter(const QString& assignAddr,int networkLength,const WCHAR* adapterName=L"QNetLink_virtualAdapter")
    {
        GUID guid;
        HRESULT hr = CoCreateGuid(&guid);
        if (FAILED(hr))return 0;
        WINTUN_ADAPTER_HANDLE adapter;
        if (!(adapter=functionLoader->CreateAdapter(L"QNetLink",adapterName, &guid)))return 0;
        NET_LUID luid;functionLoader->GetAdapterLUID(adapter, &luid);
        if(!assignIpToInterface(luid,assignAddr.toStdString().data(),networkLength))
        {
            functionLoader->CloseAdapter(adapter);
            return 0;
        }
        setAdapterMtuByWinApi(luid,1300);
        return adapter;
    }
    WINTUN_SESSION_HANDLE getSession(WINTUN_ADAPTER_HANDLE adapter){
        if(!adapter)return nullptr;
        return functionLoader->StartSession(adapter, 0x400000);
    }
    void shutTun(WINTUN_SESSION_HANDLE session,WINTUN_ADAPTER_HANDLE adapter)
    {
        if(session)
            functionLoader->EndSession(session);
        if(adapter)
            functionLoader->CloseAdapter(adapter);
    }
    bool assignIpToInterface(NET_LUID luid, const QString &ipAddress,int networkLength)
    {
        MIB_UNICASTIPADDRESS_ROW row;
        InitializeUnicastIpAddressEntry(&row);
        if (inet_pton(AF_INET, ipAddress.toStdString().c_str(), &row.Address.Ipv4.sin_addr) <= 0)
        {qDebug() << "IP 地址格式错误:" << ipAddress;return false;}
        row.InterfaceLuid = luid;
        row.Address.Ipv4.sin_family = AF_INET;
        row.OnLinkPrefixLength = networkLength;
        row.DadState = IpDadStatePreferred;
        DWORD err = CreateUnicastIpAddressEntry(&row);
        if (err != NO_ERROR)
        {
            if (err == ERROR_OBJECT_ALREADY_EXISTS)
                return true;
            qDebug() << "IP 注入失败！系统错误码:" << err;
            return false;
        }
        return true;
    }
    bool static setAdapterMtuByWinApi(NET_LUID luid, DWORD mtu)
    {
        MIB_IPINTERFACE_ROW row;
        InitializeIpInterfaceEntry(&row);
        row.Family = AF_INET;
        row.InterfaceLuid = luid;
        row.NlMtu = mtu;
        DWORD result = SetIpInterfaceEntry(&row);
        if (result != NO_ERROR)
        {
            qDebug() << "SetIpInterfaceEntry 失败，错误码:" << result;
            return false;
        }
        return true;
    }
};
#endif // TUNMANAGER_H
