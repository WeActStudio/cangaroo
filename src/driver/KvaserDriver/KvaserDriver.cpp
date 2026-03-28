/*

  Copyright (c) 2026 Schildkroet

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

#include "KvaserDriver.h"
#include "KvaserInterface.h"
#include <core/Backend.h>
#include <driver/GenericCanSetupPage.h>

#include <canlib.h>

KvaserDriver::KvaserDriver(Backend &backend)
  : CanDriver(backend),
    setupPage(new GenericCanSetupPage())
{
    QObject::connect(&backend, &Backend::onSetupDialogCreated, setupPage, &GenericCanSetupPage::onSetupDialogCreated);
    canInitializeLibrary();
}

KvaserDriver::~KvaserDriver()
{
}

QString KvaserDriver::getName()
{
    return "Kvaser";
}

bool KvaserDriver::update()
{
    deleteAllInterfaces();

    int numChannels = 0;
    canStatus status = canGetNumberOfChannels(&numChannels);
    if (status != canOK) {
        log_error(QString("KvaserDriver: canGetNumberOfChannels failed: %1").arg(status));
        return false;
    }

    for (int ch = 0; ch < numChannels; ch++) {
        char devName[256] = {};
        canGetChannelData(ch, canCHANNELDATA_DEVDESCR_ASCII, devName, sizeof(devName));
        QString name = QString("%1 (ch%2)").arg(devName).arg(ch);
        createOrUpdateInterface(ch, name);
    }

    return true;
}

KvaserInterface *KvaserDriver::createOrUpdateInterface(int channel, QString name)
{
    for (auto *intf : getInterfaces()) {
        KvaserInterface *kif = dynamic_cast<KvaserInterface*>(intf);
        if (kif && kif->getChannel() == channel) {
            kif->setName(name);
            return kif;
        }
    }

    KvaserInterface *kif = new KvaserInterface(this, channel, name);
    addInterface(kif);
    return kif;
}
