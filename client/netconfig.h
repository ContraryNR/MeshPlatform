#ifndef NETCONFIG_H
#define NETCONFIG_H
#include <QString>
#include <QHostAddress>

class netConfig
{
    public:
        QString ip;int port;
        netConfig():ip(),port(0){}
        netConfig(const QString& IP,int PORT):ip(IP),port(PORT){}
        netConfig(const netConfig& oldOne):ip(oldOne.ip),port(oldOne.port){}
        bool isValid(){return checkIPv4(ip)&&(port>0&&port<65535);}
        bool checkIPv4(const QString & ipStr)
        {
            if(ipStr.isEmpty())
                return false;
            QHostAddress ip;
            if(ip.setAddress(ipStr)&&ip.protocol()==QAbstractSocket::IPv4Protocol)
                return true;
            else
                return false;
        }
};

#endif // NETCONFIG_H
