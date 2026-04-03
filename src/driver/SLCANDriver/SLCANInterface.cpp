/*

  Copyright (c) 2022 Ethan Zonca

  This file is part of cangaroo.

  cangaroo is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 2 of the License, or
  (at your option) any later version.

  cangaroo is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with cangaroo.  If not, see <http://www.gnu.org/licenses/>.

*/

#include "SLCANInterface.h"
#include "qapplication.h"
#include "qdebug.h"

#include <core/Backend.h>
#include <core/MeasurementInterface.h>
#include <core/CanMessage.h>

#include <iostream>
#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <fcntl.h>
#include <QString>
#include <QStringList>
#include <QProcess>
#include <QtSerialPort/QSerialPort>
#include <QtSerialPort/QSerialPortInfo>
#include <QThread>

SLCANInterface::SLCANInterface(SLCANDriver *driver, int index, QString name, bool fd_support, uint32_t manufacturer, uint32_t model)
  : CanInterface((CanDriver *)driver),
    _manufacturer(manufacturer),
    _model(model),
    _idx(index),
    _isOpen(false),
    _isOffline(false),
    _serport(NULL),
    _name(name),
    _ts_mode(ts_mode_SIOCSHWTSTAMP),
    _send_wait_respond(0)
{
    // Set defaults
    _settings.setBitrate(500000);
    _settings.setSamplePoint(875);

    _config.supports_canfd = fd_support;
    _config.supports_timing = false;

    if(fd_support)
    {
        _settings.setFdBitrate(2000000);
        _settings.setFdSamplePoint(750);
    }

    _status.can_state = state_bus_off;
    _status.rx_count = 0;
    _status.rx_errors = 0;
    _status.rx_overruns = 0;
    _status.tx_count = 0;
    _status.tx_errors = 0;
    _status.tx_dropped = 0;

    _readMessage_datetime = QDateTime::currentDateTime();

    _readMessage_datetime_run = QDateTime::currentDateTime();

    _can_msg_queue.clear();
    _can_msg_tx_queue.clear();
}

SLCANInterface::~SLCANInterface() {
}

QString SLCANInterface::getDetailsStr() const {
    if(_manufacturer == CANable)
    {
        if(_config.supports_canfd)
        {
            return "CANable with CANFD support";
        }
        else
        {
            return "CANable with standard CAN support";
        }
    }
    else if(_manufacturer == WeActStudio)
    {
        if(_config.supports_canfd)
        {
            if(_model == USB2CANFDV1)
                return "WeAct Studio USB2CANFD V1, CANFD support";
            else if(_model == USB2CANFDV2)
                return "WeAct Studio USB2CANFD V2, CANFD support";
            else
                return "Unkonwn, CANFD support";
        }
        else
        {
            return "WeAct Studio USB2CAN with standard CAN support";
        }
    }
    else
    {
        return "Not Support";
    }
}

QString SLCANInterface::getName() const {
	return _name;
}

void SLCANInterface::setName(QString name) {
    _name = name;
}

QList<CanTiming> SLCANInterface::getAvailableBitrates()
{
    QList<CanTiming> retval;
    QList<unsigned> bitrates;
    QList<unsigned> bitrates_fd;

    QList<unsigned> samplePoints;
    QList<unsigned> samplePoints_fd;

    if(_manufacturer == CANable)
    {
        bitrates.append({500000, 10000, 20000, 50000, 83333, 100000, 125000, 250000, 800000, 1000000});
        bitrates_fd.append({2000000, 5000000});
        samplePoints.append({875});
        samplePoints_fd.append({750});
    }
    else if(_manufacturer == WeActStudio)
    {
        bitrates.append({500000, 5000, 10000, 20000, 33333, 50000, 62500, 75000, 83333, 100000, 125000, 250000, 800000, 1000000});
        bitrates_fd.append({2000000, 1000000, 3000000, 4000000, 5000000});
        samplePoints.append({875,500,625,750});
        samplePoints_fd.append({750,875,625,500});
    }

    unsigned i=0;
    foreach (unsigned br, bitrates) {
        foreach(unsigned br_fd, bitrates_fd) {
            foreach (unsigned sp, samplePoints) {
                foreach (unsigned sp_fd, samplePoints_fd) {
                    retval << CanTiming(i++, br, br_fd, sp,sp_fd);
                }
            }
        }
    }

    return retval;
}

void SLCANInterface::applyConfig(const MeasurementInterface &mi)
{
    // Save settings for port configuration
    _settings = mi;
}

bool SLCANInterface::updateStatus()
{
    return false;
}

bool SLCANInterface::readConfig()
{
    return false;
}

bool SLCANInterface::readConfigFromLink(rtnl_link *link)
{
    return false;
}

bool SLCANInterface::supportsTimingConfiguration()
{
    return _config.supports_timing;
}

bool SLCANInterface::supportsCanFD()
{
    return _config.supports_canfd;
}

bool SLCANInterface::supportsTripleSampling()
{
    return false;
}

unsigned SLCANInterface::getBitrate()
{
    return _settings.bitrate();
}

int SLCANInterface::getSamplePoint()
{
    return _settings.samplePoint();
}

unsigned SLCANInterface::getBitrateFD()
{
    return _settings.fdBitrate();
}

int SLCANInterface::getSamplePointFD()
{
    return _settings.fdSamplePoint();
}

uint32_t SLCANInterface::getCapabilities()
{
    uint32_t retval = 0;

    if(_manufacturer == CANable)
    {
        retval =
            CanInterface::capability_config_os |
            CanInterface::capability_auto_restart |
            CanInterface::capability_listen_only;
    }
    else if(_manufacturer == WeActStudio)
    {
        retval =
            // CanInterface::capability_config_os |
            // CanInterface::capability_auto_restart |
            CanInterface::capability_can_filter |
            CanInterface::capability_slcan_enhance_mode |
            CanInterface::capability_listen_only |
            CanInterface::capability_custom_bitrate |
            CanInterface::capability_custom_canfd_bitrate;
    }

    if (supportsCanFD()) {
        retval |= CanInterface::capability_canfd;
    }

    if (supportsTripleSampling()) {
        retval |= CanInterface::capability_triple_sampling;
    }

    return retval;
}

bool SLCANInterface::updateStatistics()
{
    return updateStatus();
}

uint32_t SLCANInterface::getState()
{
    return _status.can_state;
}

int SLCANInterface::getNumRxFrames()
{
    return _status.rx_count;
}

int SLCANInterface::getNumRxErrors()
{
    return _status.rx_errors;
}

int SLCANInterface::getNumTxFrames()
{
    return _status.tx_count;
}

int SLCANInterface::getNumTxErrors()
{
    return _status.tx_errors;
}

int SLCANInterface::getNumRxOverruns()
{
    return _status.rx_overruns;
}

int SLCANInterface::getNumTxDropped()
{
    return _status.tx_dropped;
}

int SLCANInterface::getIfIndex() {
    return _idx;
}

QString SLCANInterface::getVersion()
{
    return _version;
}

void SLCANInterface::open()
{
    if(_serport != NULL)
    {
        delete _serport;
    }

    _serport = new QSerialPort();

    _serport_mutex.lock();
    _serport->setPortName(_name);
    _serport->setBaudRate(1000000);
    _serport->setDataBits(QSerialPort::Data8);
    _serport->setParity(QSerialPort::NoParity);
    _serport->setStopBits(QSerialPort::OneStop);
    _serport->setFlowControl(QSerialPort::NoFlowControl);
    _serport->setReadBufferSize(2048);

    if (_serport->open(QIODevice::ReadWrite)) {
        //perror("Serport connected!");
        qRegisterMetaType<QSerialPort::SerialPortError>("SerialThread");
        connect(_serport, static_cast<void (QSerialPort::*)(QSerialPort::SerialPortError)>(&QSerialPort::error),  this, &SLCANInterface::handleSerialError);
        //connect(_serport, SIGNAL(readyRead()),this,SLOT(serport_readyRead()));
    } else {
        perror("Serport connect failed!");
        _serport_mutex.unlock();
        _isOpen = false;
        _isOffline = true;
        return;
    }
    _serport->flush();
    _serport->clear();

    // Close CAN port
    _serport->write("C\r", 2);
    _serport->flush();
    _serport->waitForBytesWritten(300);
    _serport->waitForReadyRead(50);

    // Get Version
    _serport->clear(QSerialPort::Input);
    _serport->write("V\r", 2);
    _serport->flush();
    _serport->waitForBytesWritten(300);
    if(_serport->waitForReadyRead(50))
    {
        qApp->processEvents();

        if(_serport->bytesAvailable())
        {
            // This is called when readyRead() is emitted
            QByteArray datas = _serport->readLine();
            _version = QString(datas).trimmed();
        }
    }

    if(_settings.isCustomBitrate())
    {
        QString _custombitrate = QString("%1").arg(_settings.customBitrate(), 6, 16,QLatin1Char('0')).toUpper();
        std::string _custombitrate_std= 'S' + _custombitrate.toStdString() + '\r';
        _serport->write(_custombitrate_std.c_str(), _custombitrate_std.length());
        _serport->flush();
    }
    else
    {
        std::cout << "   ++ Set bitrate to " << _settings.bitrate() << std::endl;
        std::cout << "   ++ Set sample point to " << _settings.samplePoint() << std::endl;

        std::string _bitrate_std = "";
        if(_settings.samplePoint() == 875)
        {
            // Set the classic CAN bitrate
            switch(_settings.bitrate())
            {
                case 1000000:
                    _bitrate_std = "S8\r";
                    break;
                case 800000:
                    _bitrate_std = "S7\r";
                    break;
                case 500000:
                    _bitrate_std = "S6\r";
                    break;
                case 250000:
                    _bitrate_std = "S5\r";
                    break;
                case 125000:
                    _bitrate_std = "S4\r";
                    break;
                case 100000:
                    _bitrate_std = "S3\r";
                    break;
                case 83333:
                    _bitrate_std = "S9\r";
                    break;
                case 75000:
                    _bitrate_std = "SA\r";
                    break;
                case 62500:
                    _bitrate_std = "SB\r";
                    break;
                case 50000:
                    _bitrate_std = "S2\r";
                    break;
                case 33333:
                    _bitrate_std = "SC\r";
                    break;
                case 20000:
                    _bitrate_std = "S1\r";
                    break;
                case 10000:
                    _bitrate_std = "S0\r";
                    break;
                case 5000:
                    _bitrate_std = "SD\r";
                    break;
                default:
                    // Default to 10k
                    _bitrate_std = "S0\r";
                    break;
            }
        }
        else if(_settings.samplePoint() == 500)
        {
            if(_manufacturer == WeActStudio)
            {
                switch(_settings.bitrate())
                {
                    case 1000000:
                        _bitrate_std = "S011D1E\r";
                        break;
                    case 800000:
                        _bitrate_std = "S012525\r";
                        break;
                    case 500000:
                        _bitrate_std = "S013B3C\r";
                        break;
                    case 250000:
                        _bitrate_std = "S023B3C\r";
                        break;
                    case 125000:
                        _bitrate_std = "S043B3C\r";
                        break;
                    case 100000:
                        _bitrate_std = "S053B3C\r";
                        break;
                    case 83333:
                        _bitrate_std = "S063B3C\r";
                        break;
                    case 75000:
                        _bitrate_std = "S054F50\r";
                        break;
                    case 62500:
                        _bitrate_std = "S083B3C\r";
                        break;
                    case 50000:
                        _bitrate_std = "S0A3B3C\r";
                        break;
                    case 33333:
                        _bitrate_std = "S087070\r";
                        break;
                    case 20000:
                        _bitrate_std = "S193B3C\r";
                        break;
                    case 10000:
                        _bitrate_std = "S323B3C\r";
                        break;
                    case 5000:
                        _bitrate_std = "S327778\r";
                        break;
                    default:
                        // Default to 10k
                        _bitrate_std = "S323B3C\r";
                        break;
                }
            }
        }
        else if(_settings.samplePoint() == 625)
        {
            if(_manufacturer == WeActStudio)
            {
                switch(_settings.bitrate())
                {
                    case 1000000:
                        _bitrate_std = "S012516\r";
                        break;
                    case 800000:
                        _bitrate_std = "S012E1C\r";
                        break;
                    case 500000:
                        _bitrate_std = "S014A2D\r";
                        break;
                    case 250000:
                        _bitrate_std = "S024A2D\r";
                        break;
                    case 125000:
                        _bitrate_std = "S044A2D\r";
                        break;
                    case 100000:
                        _bitrate_std = "S054A2D\r";
                        break;
                    case 83333:
                        _bitrate_std = "S064A2D\r";
                        break;
                    case 75000:
                        _bitrate_std = "S05633C\r";
                        break;
                    case 62500:
                        _bitrate_std = "S084A2D\r";
                        break;
                    case 50000:
                        _bitrate_std = "S0A4A2D\r";
                        break;
                    case 33333:
                        _bitrate_std = "S088C54\r";
                        break;
                    case 20000:
                        _bitrate_std = "S194A2D\r";
                        break;
                    case 10000:
                        _bitrate_std = "S324A2D\r";
                        break;
                    case 5000:
                        _bitrate_std = "S32955A\r";
                        break;
                    default:
                        // Default to 10k
                        _bitrate_std = "S324A2D\r";
                        break;
                }
            }
        }
        else if(_settings.samplePoint() == 750)
        {
            if(_manufacturer == WeActStudio)
            {
                switch(_settings.bitrate())
                {
                    case 1000000:
                        _bitrate_std = "S012C0F\r";
                        break;
                    case 800000:
                        _bitrate_std = "S013713\r";
                        break;
                    case 500000:
                        _bitrate_std = "S01591E\r";
                        break;
                    case 250000:
                        _bitrate_std = "S02591E\r";
                        break;
                    case 125000:
                        _bitrate_std = "S04591E\r";
                        break;
                    case 100000:
                        _bitrate_std = "S05591E\r";
                        break;
                    case 83333:
                        _bitrate_std = "S06591E\r";
                        break;
                    case 75000:
                        _bitrate_std = "S057728\r";
                        break;
                    case 62500:
                        _bitrate_std = "S08591E\r";
                        break;
                    case 50000:
                        _bitrate_std = "S0A591E\r";
                        break;
                    case 33333:
                        _bitrate_std = "S08A838\r";
                        break;
                    case 20000:
                        _bitrate_std = "S19591E\r";
                        break;
                    case 10000:
                        _bitrate_std = "S32591E\r";
                        break;
                    case 5000:
                        _bitrate_std = "S32B33C\r";
                        break;
                    default:
                        // Default to 10k
                        _bitrate_std = "S32591E\r";
                        break;
                }
            }
        }

        if(!_bitrate_std.empty())
        {
            _serport->write(_bitrate_std.c_str(), _bitrate_std.length());
            _serport->flush();
        }
    }

    _serport->waitForBytesWritten(300);

    // Set configured BRS rate
    if(_config.supports_canfd)
    {
        if(_settings.isCustomFdBitrate())
        {
            QString _customfdbitrate = QString("%1").arg(_settings.customFdBitrate(), 6, 16,QLatin1Char('0')).toUpper();
            std::string _customfdbitrate_std= 'Y' + _customfdbitrate.toStdString() + '\r';
            _serport->write(_customfdbitrate_std.c_str(), _customfdbitrate_std.length());
            _serport->flush();
        }
        else
        {
            std::cout << "   ++ Set FD bitrate to " << _settings.fdBitrate() << std::endl;
            std::cout << "   ++ Set FD sample point to " << _settings.fdSamplePoint() << std::endl;

            std::string _fdbitrate_std = "";
            if(_settings.fdSamplePoint() == 750)
            {
                switch(_settings.fdBitrate())
                {
                    case 1000000:
                        _fdbitrate_std = "Y1\r";
                        break;
                    case 2000000:
                        _fdbitrate_std = "Y2\r";
                        break;
                    case 3000000:
                        _fdbitrate_std = "Y3\r";
                        break;
                    case 4000000:
                        _fdbitrate_std = "Y4\r";
                        break;
                    case 5000000:
                        _fdbitrate_std = "Y5\r";
                        break;
                }
            }
            else if(_settings.fdSamplePoint() == 875)
            {
                if(_manufacturer == WeActStudio)
                {
                    switch(_settings.fdBitrate())
                    {
                        case 1000000:
                            _fdbitrate_std = "Y021904\r";
                            break;
                        case 2000000:
                            _fdbitrate_std = "Y011904\r";
                            break;
                        case 3000000:
                            _fdbitrate_std = "Y011003\r";
                            break;
                        case 4000000:
                            _fdbitrate_std = "Y010C02\r";
                            break;
                        case 5000000:
                            _fdbitrate_std = "Y010902\r";
                            break;
                    }
                }
            }
            else if(_settings.fdSamplePoint() == 625)
            {
                if(_manufacturer == WeActStudio)
                {
                    switch(_settings.fdBitrate())
                    {
                    case 1000000:
                        _fdbitrate_std = "Y02120B\r";  // 63.33%
                        break;
                    case 2000000:
                        _fdbitrate_std = "Y01120B\r";  // 63.33%
                        break;
                    case 3000000:
                        _fdbitrate_std = "Y010B08\r";  // 60%
                        break;
                    case 4000000:
                        _fdbitrate_std = "Y010806\r";  // 60%
                        break;
                    case 5000000:
                        _fdbitrate_std = "Y010704\r";  // 66.67%
                        break;
                    }
                }
            }
            else if(_settings.fdSamplePoint() == 500)
            {
                if(_manufacturer == WeActStudio)
                {
                    switch(_settings.fdBitrate())
                    {
                    case 1000000:
                        _fdbitrate_std = "Y020E0F\r";
                        break;
                    case 2000000:
                        _fdbitrate_std = "Y010E0F\r";
                        break;
                    case 3000000:
                        _fdbitrate_std = "Y01090A\r";
                        break;
                    case 4000000:
                        _fdbitrate_std = "Y010608\r";  // 46.67%
                        break;
                    case 5000000:
                        _fdbitrate_std = "Y010506\r";
                        break;
                    }
                }
            }

            if(!_fdbitrate_std.empty())
            {
                _serport->write(_fdbitrate_std.c_str(), _fdbitrate_std.length());
                _serport->flush();
            }
        }
    }
    _serport->waitForBytesWritten(300);

    // Set Listen Only Mode
    if(_settings.isListenOnlyMode())
    {
        _serport->write("M1\r", 3);
        _serport->flush();
    }
    else
    {
        _serport->write("M0\r", 3);
        _serport->flush();
    }
    _serport->waitForBytesWritten(300);

    // Set SLCAN Enhance Mode
    if(_settings.isSlcanEnhanceMode())
    {
        _serport->write("H1\r", 3);
        _serport->flush();
    }
    else
    {
        _serport->write("H0\r", 3);
        _serport->flush();
    }
    _serport->waitForBytesWritten(300);

    // Set Can Filter
    if(_settings.isFilterEnable())
    {
        QString stdFilter = QString("%1%2")
            .arg(_settings.stdFilterId(), 3, 16, QLatin1Char('0'))
            .arg(_settings.stdFilterMask(), 3, 16, QLatin1Char('0'))
            .toUpper();
        std::string _filter_std= 'f' + stdFilter.toStdString() + '\r';
        _serport->write(_filter_std.c_str(), _filter_std.length());
        _serport->flush();
        _serport->waitForBytesWritten(300);

        QString extFilter = QString("%1%2")
            .arg(_settings.extFilterId(), 8, 16, QLatin1Char('0'))
            .arg(_settings.extFilterMask(), 8, 16, QLatin1Char('0'))
            .toUpper();
        std::string _filter_ext= 'F' + extFilter.toStdString() + '\r';
        _serport->write(_filter_ext.c_str(), _filter_ext.length());
        _serport->flush();
        _serport->waitForBytesWritten(300);
    }
    else
    {
        _serport->write("f000000\r", 8);
        _serport->flush();
        _serport->waitForBytesWritten(300);
        _serport->write("F0000000000000000\r", 18);
        _serport->flush();
        _serport->waitForBytesWritten(300);
    }
    

    // Open the port
    _serport->write("O\r", 2);
    _serport->flush();
    _serport->waitForBytesWritten(300);

    // Clear serial port receiver
    if(_serport->waitForReadyRead(10))
    {
        qApp->processEvents();

        if(_serport->bytesAvailable())
        {
            // This is called when readyRead() is emitted
            _serport->readAll();
        }
    }

    _can_msg_queue.clear();
    _can_msg_tx_queue.clear();
    _send_wait_respond = 0;

    _isOpen = true;
    _isOffline = false;
    _status.can_state = state_ok;
    _status.rx_count = 0;
    _status.rx_errors = 0;
    _status.rx_overruns = 0;
    _status.tx_count = 0;
    _status.tx_errors = 0;
    _status.tx_dropped = 0;

    _rx_data.clear();

    // Release port mutex
    _serport_mutex.unlock();
}

void SLCANInterface::handleSerialError(QSerialPort::SerialPortError error)
{
    if (error == QSerialPort::ResourceError) {
        perror("error");

        _isOffline = true;
    }

    QString  ERRORString = NULL ;
    switch (error) {
    case QSerialPort::NoError:
        ERRORString=  "No Error";
        break;
    case QSerialPort::DeviceNotFoundError:
        ERRORString= "Device Not Found";
        break;
    case QSerialPort::PermissionError:
        ERRORString= "Permission Denied";
        break;
    case QSerialPort::OpenError:
        ERRORString= "Open Error";
        break;
    case QSerialPort::ParityError:
        ERRORString= "Parity Error";
        break;
    case QSerialPort::FramingError:
        ERRORString= "Framing Error";
        break;
    case QSerialPort::BreakConditionError:
        ERRORString= "Break Condition";
        break;
    case QSerialPort::WriteError:
        ERRORString= "Write Error";
        break;
    case QSerialPort::ReadError:
        ERRORString= "Read Error";
        break;
    case QSerialPort::ResourceError:
        ERRORString= "Resource Error";
        break;
    case QSerialPort::UnsupportedOperationError:
        ERRORString= "Unsupported Operation";
        break;
    case QSerialPort::UnknownError:
        ERRORString= "Unknown Error";
        break;
    case QSerialPort::TimeoutError:
        //ERRORString= "Timeout Error";
        break;
    case QSerialPort::NotOpenError:
        ERRORString= "Not Open Error";
        break;
    default:
        ERRORString= "Other Error";
    }
    if(ERRORString != NULL)
        std::cout << "SerialPortWorker::errorOccurred  ,info is  " << ERRORString.toStdString() << std::endl;
}

void SLCANInterface::close()
{
    _serport_mutex.lock();

    _isOpen = false;
    _status.can_state = state_bus_off;

    if (_serport->isOpen())
    {
        // Close CAN port
        _serport->write("C\r", 2);
        _serport->flush();
        _serport->waitForBytesWritten(300);
        _serport->waitForReadyRead(10);
        _serport->clear();
        _serport->close();
    }

    _can_msg_queue.clear();
    _can_msg_tx_queue.clear();

    _serport_mutex.unlock();
}

bool SLCANInterface::isOpen()
{
    return _isOpen;
}

void SLCANInterface::sendMessage(const CanMessage &msg)
{
    if(_settings.isSlcanEnhanceMode())
    {
        eh_sendMessage(msg);
        return;
    }

    _serport_mutex.lock();
    // SLCAN_MTU plus null terminator
    can_msg_t can_msg;

    uint8_t msg_idx = 0;

    // Message is FD
    // Add character for frame type
    if(msg.isFD())
    {
        if(msg.isBRS())
        {
            can_msg.buf[msg_idx] = 'b';

        }
        else
        {
            can_msg.buf[msg_idx] = 'd';
        }
    }

    // Message is not FD
    // Add character for frame type
    else
    {
        if (msg.isRTR()) {
            can_msg.buf[msg_idx] = 'r';
        }
        else
        {
            can_msg.buf[msg_idx] = 't';
        }
    }

    // Assume standard identifier
    uint8_t id_len = SLCAN_STD_ID_LEN;
    uint32_t tmp = msg.getId();

    // Check if extended
    if (msg.isExtended())
    {
        // Convert first char to upper case for extended frame
        can_msg.buf[msg_idx] -= 32;
        id_len = SLCAN_EXT_ID_LEN;
    }
    msg_idx++;

    // Add identifier to buffer
    for(uint8_t j = id_len; j > 0; j--)
    {
        // Add nibble to buffer
        can_msg.buf[j] = (tmp & 0xF);
        tmp = tmp >> 4;
        msg_idx++;
    }

    // Sanity check length
    int8_t bytes = msg.getLength();

    if(bytes < 0)
        return;
    if(bytes > 64)
        return;

    // If canfd
    if(bytes > 8)
    {
        switch(bytes)
        {
        case 12:
            bytes = 0x9;
            break;
        case 16:
            bytes = 0xA;
            break;
        case 20:
            bytes = 0xB;
            break;
        case 24:
            bytes = 0xC;
            break;
        case 32:
            bytes = 0xD;
            break;
        case 48:
            bytes = 0xE;
            break;
        case 64:
            bytes = 0xF;
            break;
        }
    }

    // Add DLC to buffer
    can_msg.buf[msg_idx++] = bytes;

    // Add data bytes
    if(!msg.isRTR())
    {
        for (uint8_t j = 0; j < msg.getLength(); j++)
        {
            can_msg.buf[msg_idx++] = (msg.getByte(j) >> 4);
            can_msg.buf[msg_idx++] = (msg.getByte(j) & 0x0F);
        }
    }

    // Convert to ASCII (2nd character to end)
    for (uint8_t j = 1; j < msg_idx; j++)
    {
        if (can_msg.buf[j] < 0xA) {
            can_msg.buf[j] += 0x30;
        } else {
            can_msg.buf[j] += 0x37;
        }
    }

    // Add CR for slcan EOL
    can_msg.buf[msg_idx++] = '\r';

    // Ensure null termination
    can_msg.buf[msg_idx] = '\0';

    can_msg.length = msg_idx;

    _can_msg_queue.append(can_msg);
    _can_msg_tx_queue.append(msg);

    _serport_mutex.unlock();

}

bool SLCANInterface::readMessage(QList<CanMessage> &msglist, unsigned int timeout_ms)
{
    CanMessage msgtx;
    QDateTime datetime;

    int sleep_ms = 1;

    if(_isOffline == true)
    {
        if(_isOpen)
            close();
        return false;
    }
    else
    {
        datetime = QDateTime::currentDateTime();
        if(datetime.toMSecsSinceEpoch() - _readMessage_datetime.toMSecsSinceEpoch() > 3000)
        {
            _status.can_state = state_ok;
            _send_wait_respond = 0;
        }
    }

    // Transmit all items that are queued
    can_msg_t tmp;

    QList<can_msg_t>::iterator it;
    _serport_mutex.lock();
    for(it = _can_msg_queue.begin(); it<_can_msg_queue.end();it++)
    {
        if(_can_msg_queue.empty())
        {
            std::cout << "msg empty1" << std::endl;
            break;
        }

        // Consume first item
        tmp = _can_msg_queue.front();
        _can_msg_queue.pop_front();

        // Write string to serial device
        if(_serport->write((const char *)tmp.buf, tmp.length)==tmp.length)
        {
            _send_wait_respond ++;
            _readMessage_datetime = QDateTime::currentDateTime();
        }
        else
        {
            _status.tx_errors ++;
            //_send_wait_respond = 0;

            if(_can_msg_tx_queue.empty() == false)
            {
                _can_msg_tx_queue.pop_front();
            }
        }
        
        sleep_ms = 0;

        //_serport->flush();
        _serport->waitForBytesWritten(300);

        if(it >= _can_msg_queue.end())
        {
            std::cout << "msg empty" << std::endl;
            //_can_msg_queue.clear();
            break;
        }
    }
    _serport_mutex.unlock();

    // RX doesn't work on windows unless we call this for some reason
    _rxbuf_mutex.lock();
    if(_serport->waitForReadyRead(sleep_ms))
    {
        qApp->processEvents();

        if(_serport->bytesAvailable())
        {
            // This is called when readyRead() is emitted
            _rx_data.append(_serport->readAll());
        }
    }

    bool ret = true;

    if(_rx_data.isEmpty() == false)
    {
        static uint8_t raw_length = 0;
        static QElapsedTimer rx_timer;
        if(rx_timer.elapsed() >= 10) {
            _rx_state = PARSE_IDLE;
        }
        for (int i = 0; i < _rx_data.size(); ++i) {
            if(_rx_state == PARSE_IDLE)
            {
                _rx_data_index = 0;
                if ((uint8_t)_rx_data.at(i) > SLCAN_EH_START)
                {
                    if(_settings.isSlcanEnhanceMode())
                        _rx_state = PARSE_RAW_LENGTH;
                }
                else
                {
                    if (_rx_data.at(i) == SLCAN_RET_OK)
                    {
                        if(_send_wait_respond)
                        {
                            datetime = QDateTime::currentDateTime();
                            if(datetime.toMSecsSinceEpoch() - _readMessage_datetime.toMSecsSinceEpoch() < 200)
                            {
                                _status.tx_count ++;
                                _status.can_state = state_tx_success;
                            }
                            _send_wait_respond --;

                            if(_can_msg_tx_queue.empty() == false)
                            {
                                if(_status.can_state == state_tx_success)
                                {
                                    msgtx.cloneFrom(_can_msg_tx_queue.front());
                                    if(msgtx.isShow())
                                        msglist.append(msgtx);
                                }
                                if(_can_msg_tx_queue.empty() == false)
                                    _can_msg_tx_queue.pop_front();
                            }
                        }
                        _rx_state = PARSE_IDLE;
                    }
                    else if (_rx_data.at(i) == SLCAN_RET_ERR)
                    {
                        if(_send_wait_respond)
                        {
                            datetime = QDateTime::currentDateTime();
                            if(datetime.toMSecsSinceEpoch() - _readMessage_datetime.toMSecsSinceEpoch() < 200)
                            {
                                _status.tx_errors ++;
                                _status.can_state = state_tx_fail;
                            }
                            _send_wait_respond --;

                            if(_can_msg_tx_queue.empty() == false)
                                _can_msg_tx_queue.pop_front();
                        }
                        _rx_state = PARSE_IDLE;
                    }
                    else
                        _rx_state = PARSE_SLCAN;
                }
                _rx_frame[_rx_data_index++] = (uint8_t)_rx_data.at(i);
                rx_timer.start();
            }
            else if(_rx_state == PARSE_SLCAN)
            {
                //  Process one whole buffer
                if (_rx_data.at(i) == SLCAN_RET_OK)
                {
                    CanMessage msg;
                    ret = parseMessage(msg,_rx_frame, _rx_data_index);
                    if(ret == true)
                    {
                        msglist.append(msg);
                        _status.rx_count ++;
                    }
                    _rx_state = PARSE_IDLE;
                }
                else
                {
                    // Check for overflow of buffer
                    if (_rx_data_index >= SLCAN_MTU)
                    {
                        // TODO: Return here and discard this CDC buffer?
                        _rx_state = PARSE_IDLE;
                    }
                    else
                    {
                        _rx_frame[_rx_data_index++] = (uint8_t)_rx_data.at(i);
                    }
                }
            }
            else if(_rx_state == PARSE_RAW_LENGTH)
            {
                _rx_frame[_rx_data_index++] = (uint8_t)_rx_data.at(i);
                raw_length = (uint8_t)_rx_data.at(i) + 2;
                if(raw_length >= SLCAN_MTU)
                {
                    _rx_state = PARSE_IDLE;
                }
                else
                {
                    _rx_state = PARSE_RAW;
                }
            }
            else if(_rx_state == PARSE_RAW)
            {
                _rx_frame[_rx_data_index++] = (uint8_t)_rx_data.at(i);
                if(_rx_data_index == raw_length)
                {
                    CanMessage msg;
                    ret = eh_parseMessage(msg,_rx_frame);

                    if(ret == true)
                    {
                        msglist.append(msg);
                        _status.rx_count ++;
                    }
                    _rx_state = PARSE_IDLE;
                }
            }

        }
        _rx_data.clear();
    }
    _rxbuf_mutex.unlock();
    return ret;
}

int8_t SLCANInterface::hal_dlc_code_to_bytes(uint8_t hal_dlc_code)
{
    if(hal_dlc_code <= 8)
        return (int8_t)hal_dlc_code;

    switch(hal_dlc_code)
    {
    case 0x9:
        return 12;
        break;
    case 0xA:
        return 16;
        break;
    case 0xB:
        return 20;
        break;
    case 0xC:
        return 24;
        break;
    case 0xD:
        return 32;
        break;
    case 0xE:
        return 48;
        break;
    case 0xF:
        return 64;
        break;
    default:
        return -1;
        break;
    }
}

bool SLCANInterface::parseMessage(CanMessage &msg, uint8_t *buf, uint8_t len)
{
    // Set timestamp to current time
    struct timeval tv;
    gettimeofday(&tv,NULL);
    msg.setTimestamp(tv);

    // Defaults
    msg.setErrorFrame(0);
    msg.setInterfaceId(getId());
    msg.setId(0);
    msg.setRTR(false);
    msg.setFD(false);
    msg.setBRS(false);
    msg.setRX(true);

    // Convert from ASCII (2nd character to end)
    for (int i = 1; i < len; i++)
    {
        // Lowercase letters
        if(buf[i] >= 'a')
            buf[i] = buf[i] - 'a' + 10;
        // Uppercase letters
        else if(buf[i] >= 'A')
            buf[i] = buf[i] - 'A' + 10;
        // Numbers
        else
            buf[i] = buf[i] - '0';
    }

    bool is_extended = false;
    bool is_rtr = false;

    // Handle each incoming command
    switch(buf[0])
    {
        // Transmit data frame command
        case 't':
        {
            is_extended = false;
        }
        break;
        case 'T':
        {
            is_extended = true;
        }
        break;

        // Transmit remote frame command
        case 'r':
        {
            is_extended = false;
            is_rtr = true;
        }
        break;
        case 'R':
        {
            is_extended = true;
            is_rtr = true;
        }
        break;

        // CANFD transmit - no BRS
        case 'd':
        {
            is_extended = false;
            msg.setFD(true);
            msg.setBRS(false);
        }
        break;
        case 'D':
        {
            is_extended = true;
            msg.setFD(true);
            msg.setBRS(false);
        }
        break;

        // CANFD transmit - with BRS
        case 'b':
        {
            is_extended = false;
            msg.setFD(true);
            msg.setBRS(true);
        }
        break;
        case 'B':
        {
            is_extended = true;
            msg.setFD(true);
            msg.setBRS(true);
        }
        break;

        // Invalid command
        default:
        {
            return false;
        }
    }

    // Start parsing at second byte (skip command byte)
    uint8_t parse_loc = 1;

    // Default to standard id len
    uint8_t id_len = SLCAN_STD_ID_LEN;

    // Update length if message is extended ID
    if(is_extended)
        id_len = SLCAN_EXT_ID_LEN;

    uint32_t id_tmp = 0;

    // Iterate through ID bytes
    while(parse_loc <= id_len)
    {
        id_tmp <<= 4;
        id_tmp += buf[parse_loc++];
    }

    msg.setId(id_tmp);
    msg.setExtended(is_extended);
    msg.setRTR(is_rtr);

    // Attempt to parse DLC and check sanity
    uint8_t dlc_code_raw = buf[parse_loc++];

    // If dlc is too long for an FD frame
    if(msg.isFD() && dlc_code_raw > 0xF)
    {
        return false;
    }
    if(!msg.isFD() && dlc_code_raw > 0x8)
    {
        return false;
    }

    int8_t bytes_in_msg = hal_dlc_code_to_bytes(dlc_code_raw);
    if(bytes_in_msg<0)
    {
        perror("Invalid Dlc length");
        return false;
    }

    msg.setLength((uint8_t)bytes_in_msg);

    // Parse data
    // TODO: Guard against walking off the end of the string!
    uint8_t tx_msg_len = 1 + id_len + 1;
    if(!msg.isRTR())
        tx_msg_len += bytes_in_msg << 1;
    
    if(tx_msg_len != len)
    {
        perror("Invalid message length");
        return false;
    }

    if(!msg.isRTR())
    {
        for (uint8_t i = 0; i < bytes_in_msg; i++)
        {
            msg.setByte(i,  (buf[parse_loc] << 4) + buf[parse_loc+1]);
            parse_loc += 2;
        }
    }

    return true;
}

void SLCANInterface::eh_sendMessage(const CanMessage &msg)
{
    _serport_mutex.lock();

    int32_t msg_len = 0;

    can_msg_t can_msg;

    slcan_eh_msg_t *slmsg = (slcan_eh_msg_t *)can_msg.buf;

    // Handle classic CAN frames
    if (!msg.isFD())
    {
        // Add character for frame type
        if (!msg.isRTR())
        {
            slmsg->header = SLCAN_STD_HEADER + SLCAN_EH_START;
        }
        else
        {
            slmsg->header = SLCAN_STD_REMOTE_HEADER + SLCAN_EH_START;
        }
    }
    // Handle FD CAN frames
    else
    {
        // FD doesn't support remote frames so this must be a data frame

        // Frame with BRS enabled
        if (msg.isBRS())
        {
            slmsg->header = SLCAN_STD_FDBRS_HEADER + SLCAN_EH_START;
        }
        // Frame with BRS disabled
        else
        {
            slmsg->header = SLCAN_STD_FD_HEADER + SLCAN_EH_START;
        }
    }

    int8_t bytes = msg.getLength();
    if(bytes < 0)
        return;
    if(bytes > 64)
        return;

    // If canfd
    if(bytes > 8)
    {
        switch(bytes)
        {
        case 12:
            bytes = 0x9;
            break;
        case 16:
            bytes = 0xA;
            break;
        case 20:
            bytes = 0xB;
            break;
        case 24:
            bytes = 0xC;
            break;
        case 32:
            bytes = 0xD;
            break;
        case 48:
            bytes = 0xE;
            break;
        case 64:
            bytes = 0xF;
            break;
        }
    }

    if (msg.isExtended())
    {
        // Convert first char to upper case for extended frame
        slmsg->header -= 32;

        slmsg->frame.ext_frame.ext_id = msg.getId();
        slmsg->frame.ext_frame.dlc = bytes;

        if(!msg.isRTR())
        {
            for (uint8_t j = 0; j < msg.getLength(); j++)
            {
                slmsg->frame.ext_frame.data[j] = msg.getByte(j);
            }
            msg_len = SLCAN_EH_EXT_ID_LEN + 1 + msg.getLength();
        }
        else
        {
            msg_len = SLCAN_EH_EXT_ID_LEN + 1;
        }

        slmsg->length = msg_len;
    }
    else
    {
        slmsg->frame.std_frame.std_id = msg.getId();
        slmsg->frame.std_frame.dlc = bytes;

        if(!msg.isRTR())
        {
            for (uint8_t j = 0; j < msg.getLength(); j++)
            {
                slmsg->frame.std_frame.data[j] = msg.getByte(j);
            }
            msg_len = SLCAN_EH_STD_ID_LEN + 1 + msg.getLength();
        }
        else
        {
            msg_len = SLCAN_EH_STD_ID_LEN + 1;
        }

        slmsg->length = msg_len;
    }

    can_msg.length = slmsg->length + 2;

    _can_msg_queue.append(can_msg);
    _can_msg_tx_queue.append(msg);

    _serport_mutex.unlock();
}

bool SLCANInterface::eh_parseMessage(CanMessage &msg, uint8_t *buf)
{
    uint8_t id_len = 0;
    uint8_t data_len = 0;
    uint8_t dlc = 0;
    uint8_t *data;

    slcan_eh_msg_t *slmsg = (slcan_eh_msg_t *)buf;

    struct timeval tv;
    gettimeofday(&tv,NULL);
    msg.setTimestamp(tv);

    // Defaults
    msg.setErrorFrame(0);
    msg.setInterfaceId(getId());
    msg.setId(0);
    msg.setExtended(false);
    msg.setRTR(false);
    msg.setFD(false);
    msg.setBRS(false);
    msg.setRX(true);

    if(!_settings.isSlcanEnhanceMode())
        return false;

    switch (slmsg->header)
    {
    case SLCAN_STD_HEADER + SLCAN_EH_START:
        id_len = SLCAN_EH_STD_ID_LEN;
        msg.setId(slmsg->frame.std_frame.std_id);
        dlc = slmsg->frame.std_frame.dlc;
        data = slmsg->frame.std_frame.data;
        data_len = slmsg->length - id_len - 1;
        break;
    case SLCAN_EXT_HEADER + SLCAN_EH_START:
        id_len = SLCAN_EH_EXT_ID_LEN;
        msg.setId(slmsg->frame.ext_frame.ext_id);
        msg.setExtended(true);
        dlc = slmsg->frame.ext_frame.dlc;
        data = slmsg->frame.ext_frame.data;
        data_len = slmsg->length - id_len - 1;
        break;
    case SLCAN_STD_REMOTE_HEADER + SLCAN_EH_START:
        msg.setId(slmsg->frame.std_frame.std_id);
        msg.setRTR(true);
        dlc = slmsg->frame.std_frame.dlc;
        data = slmsg->frame.std_frame.data;
        data_len = 0;
        break;
    case SLCAN_EXT_REMOTE_HEADER + SLCAN_EH_START:
        msg.setId(slmsg->frame.ext_frame.ext_id);
        msg.setExtended(true);
        msg.setRTR(true);
        dlc = slmsg->frame.ext_frame.dlc;
        data = slmsg->frame.ext_frame.data;
        data_len = 0;
        break;
    case SLCAN_STD_FD_HEADER + SLCAN_EH_START:
        id_len = SLCAN_EH_STD_ID_LEN;
        msg.setFD(true);
        msg.setId(slmsg->frame.std_frame.std_id);
        dlc = slmsg->frame.std_frame.dlc;
        data = slmsg->frame.std_frame.data;
        data_len = slmsg->length - id_len - 1;
        break;
    case SLCAN_EXT_FD_HEADER + SLCAN_EH_START:
        id_len = SLCAN_EH_EXT_ID_LEN;
        msg.setFD(true);
        msg.setId(slmsg->frame.ext_frame.ext_id);
        msg.setExtended(true);
        dlc = slmsg->frame.ext_frame.dlc;
        data = slmsg->frame.ext_frame.data;
        data_len = slmsg->length - id_len - 1;
        break;
    case SLCAN_STD_FDBRS_HEADER + SLCAN_EH_START:
        id_len = SLCAN_EH_STD_ID_LEN;
        msg.setFD(true);
        msg.setId(slmsg->frame.std_frame.std_id);
        msg.setBRS(true);
        dlc = slmsg->frame.std_frame.dlc;
        data = slmsg->frame.std_frame.data;
        data_len = slmsg->length - id_len - 1;
        break;
    case SLCAN_EXT_FDBRS_HEADER + SLCAN_EH_START:
        id_len = SLCAN_EH_EXT_ID_LEN;
        msg.setFD(true);
        msg.setId(slmsg->frame.ext_frame.ext_id);
        msg.setExtended(true);
        msg.setBRS(true);
        dlc = slmsg->frame.ext_frame.dlc;
        data = slmsg->frame.ext_frame.data;
        data_len = slmsg->length - id_len - 1;
        break;

    default:
        return false;
    }

    // Set TX frame DLC according to HAL
    int8_t bytes_in_msg = hal_dlc_code_to_bytes(dlc);
    if (bytes_in_msg < 0)
    {
        perror("Invalid Dlc length");
        return false;
    }
    msg.setLength((uint8_t)bytes_in_msg);

    // Set TX frame data
    if(!msg.isRTR())
    {
        if(bytes_in_msg == data_len)
        {
            for (uint8_t i = 0; i < data_len; i++)
            {
                msg.setByte(i,  data[i]);
            }
        }
        else
        {

            perror("Invalid message length");
            return false;

        }
    }

    return true;
}
