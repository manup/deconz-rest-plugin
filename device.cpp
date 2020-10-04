#include <QTimerEvent>
#include "de_web_plugin_private.h" // todo hack, remove later
#include "device.h"
#include "device_descriptions.h"
#include "event.h"
#include "zdp.h"

const int MinMacPollRxOn = 8000; // 7680 ms + some space for timeout

void DEV_InitStateHandler(Device *device, const Event &event)
{
    if (event.what() != RAttrLastSeen)
    {
        DBG_Printf(DBG_INFO, "DEV Init event %s/0x%016llX/%s\n", event.resource(), event.deviceKey(), event.what());
    }

    if (event.what() == REventPoll || event.what() == REventAwake || event.what() == RConfigReachable  || event.what() == REventStateTimeout)
    {
        // lazy reference to deCONZ::Node
        if (!device->node())
        {
            device->m_node = getCoreNode(device->key());
        }

        if (device->node())
        {
            device->item(RAttrExtAddress)->setValue(device->node()->address().ext());
            device->item(RAttrNwkAddress)->setValue(device->node()->address().nwk());

            if (device->node()->nodeDescriptor().manufacturerCode() == VENDOR_DDEL && device->node()->address().nwk() == 0x0000)
            {
                return; // ignore coordinaor for now
            }

            // got a node, jump to verification
            if (!device->node()->nodeDescriptor().isNull() || device->reachable())
            {
                device->setState(DEV_NodeDescriptorStateHandler);
            }
        }
        else
        {
            DBG_Printf(DBG_INFO, "DEV Init no node found: 0x%016llX\n", event.deviceKey());
        }
    }
}

void DEV_IdleStateHandler(Device *device, const Event &event)
{
    Q_UNUSED(device)
    if (event.what() == RAttrLastSeen || event.what() == REventPoll)
    {
         // don't print logs
    }
    else if (event.resource() ==  device->prefix())
    {
        DBG_Printf(DBG_INFO, "DEV Idle event %s/0x%016llX/%s\n", event.resource(), event.deviceKey(), event.what());
    }
    else
    {
        DBG_Printf(DBG_INFO, "DEV Idle event %s/0x%016llX/%s\n", event.resource(), event.deviceKey(), event.what());
    }

    if (event.what() == REventStateTimeout)
    {
//        device->setState(initStateHandler);
    }
}

void DEV_NodeDescriptorStateHandler(Device *device, const Event &event)
{
    if (event.what() == REventStateEnter)
    {
        if (!device->node()->nodeDescriptor().isNull())
        {
            DBG_Printf(DBG_INFO, "ZDP node descriptor verified: 0x%016llX\n", device->key());
            device->setState(DEV_ActiveEndpointsStateHandler);
        }
        else if (!device->reachable())
        {
            device->setState(DEV_InitStateHandler);
        }
        else
        {
            zdpSendNodeDescriptorReq(device->item(RAttrNwkAddress)->toNumber(), deCONZ::ApsController::instance());
            device->startStateTimer(MinMacPollRxOn);
        }
    }
    else if (event.what() == REventNodeDescriptor)
    {
        device->stopStateTimer();
        device->setState(DEV_InitStateHandler);
        device->plugin()->enqueueEvent(Event(device->prefix(), REventAwake, 0, device->key()));
    }
    else if (event.what() == REventStateTimeout)
    {
        DBG_Printf(DBG_INFO, "read ZDP node descriptor timeout: 0x%016llX\n", device->key());
        device->setState(DEV_InitStateHandler);
    }
}

void DEV_ActiveEndpointsStateHandler(Device *device, const Event &event)
{
    if (event.what() == REventStateEnter)
    {
        if (!device->node()->endpoints().empty())
        {
            DBG_Printf(DBG_INFO, "ZDP active endpoints verified: 0x%016llX\n", device->key());
            device->setState(DEV_SimpleDescriptorStateHandler);
        }
        else if (!device->reachable())
        {
            device->setState(DEV_InitStateHandler);
        }
        else
        {
            zdpSendActiveEndpointsReq(device->item(RAttrNwkAddress)->toNumber(), deCONZ::ApsController::instance());
            device->startStateTimer(MinMacPollRxOn);
        }
    }
    else if (event.what() == REventActiveEndpoints)
    {
        device->stopStateTimer();
        device->setState(DEV_InitStateHandler);
        device->plugin()->enqueueEvent(Event(device->prefix(), REventAwake, 0, device->key()));
    }
    else if (event.what() == REventStateTimeout)
    {
        DBG_Printf(DBG_INFO, "read ZDP active endpoints timeout: 0x%016llX\n", device->key());
        device->setState(DEV_InitStateHandler);
    }
}

void DEV_SimpleDescriptorStateHandler(Device *device, const Event &event)
{
    if (event.what() == REventStateEnter)
    {
        quint8 needFetchEp = 0x00;

        for (const auto ep : device->node()->endpoints())
        {
            deCONZ::SimpleDescriptor sd;
            if (device->node()->copySimpleDescriptor(ep, &sd) != 0 || sd.deviceId() == 0xffff)
            {
                needFetchEp = ep;
                break;
            }
        }

        if (needFetchEp == 0x00)
        {
            DBG_Printf(DBG_INFO, "ZDP simple descriptors verified: 0x%016llX\n", device->key());
            device->setState(DEV_ModelIdStateHandler);
        }
        else if (!device->reachable())
        {
            device->setState(DEV_InitStateHandler);
        }
        else
        {
            zdpSendSimpleDescriptorReq(device->item(RAttrNwkAddress)->toNumber(), needFetchEp, deCONZ::ApsController::instance());
            device->startStateTimer(MinMacPollRxOn);
        }
    }
    else if (event.what() == REventSimpleDescriptor)
    {
        device->stopStateTimer();
        device->setState(DEV_InitStateHandler);
        device->plugin()->enqueueEvent(Event(device->prefix(), REventAwake, 0, device->key()));
    }
    else if (event.what() == REventStateTimeout)
    {
        DBG_Printf(DBG_INFO, "read ZDP simple descriptor timeout: 0x%016llX\n", device->key());
        device->setState(DEV_InitStateHandler);
    }
}

void DEV_ModelIdStateHandler(Device *device, const Event &event)
{
    if (event.what() == REventStateEnter)
    {
        auto *modelId = device->item(RAttrModelId);
        Q_ASSERT(modelId);

        for (const auto rsub : device->subDevices())
        {
            if (!modelId->toString().isEmpty())
            {
                break;
            }

            auto *item = rsub->item(RAttrModelId);
            if (item && !item->toString().isEmpty())
            {
                // copy modelId from sub-device into device
                modelId->setValue(item->toString());
                break;
            }
        }

        if (!modelId->toString().isEmpty())
        {
            device->setState(DEV_GetDeviceDescriptionHandler);
        }
        else if (!device->reachable())
        {
            device->setState(DEV_InitStateHandler);
        }
        else
        {
            quint8 basicClusterEp = 0x00;

            for (const auto ep : device->node()->endpoints())
            {
                deCONZ::SimpleDescriptor sd;
                if (device->node()->copySimpleDescriptor(ep, &sd) == 0)
                {
                    const auto *cluster = sd.cluster(0x0000, deCONZ::ServerCluster);
                    if (cluster)
                    {
                        basicClusterEp = ep;
                        break;
                    }
                }
            }

            if (basicClusterEp != 0x00 && modelId)
            {
                modelId->setReadParameters({QLatin1String("readGenericAttribute/4"), basicClusterEp, 0x0000, 0x0005, 0x0000});
                modelId->setParseParameters({QLatin1String("parseGenericAttribute/4"), basicClusterEp, 0x0000, 0x0005, "$raw"});
                auto readFunction = getReadFunction(readFunctions, modelId->readParameters());

                if (readFunction && readFunction(device, modelId, deCONZ::ApsController::instance()))
                {

                }
                else
                {
                    DBG_Printf(DBG_INFO, "Failed to read %s: 0x%016llX on endpoint: 0x%02X\n", modelId->descriptor().suffix, device->key(), basicClusterEp);
                }
            }
            else
            {
                DBG_Printf(DBG_INFO, "TODO no basic cluster found to read modelId: 0x%016llX\n", device->key());
            }

            device->startStateTimer(MinMacPollRxOn);
        }
    }
    else if (event.what() == RAttrModelId)
    {
        DBG_Printf(DBG_INFO, "OK received modelId: 0x%016llX\n", device->key());
        device->stopStateTimer();
        device->setState(DEV_InitStateHandler); // ok re-evaluate
        device->plugin()->enqueueEvent(Event(device->prefix(), REventAwake, 0, device->key()));
    }
    else if (event.what() == REventStateTimeout)
    {
        DBG_Printf(DBG_INFO, "read ZCL read modelId timeout: 0x%016llX\n", device->key());
        device->setState(DEV_InitStateHandler);
    }
}

QString uniqueIdFromTemplate(const QStringList &templ, const quint64 extAddress)
{
    bool ok = false;
    quint8 endpoint = 0;
    quint16 clusterId = 0;

    // <mac>-<endpoint>
    // <mac>-<endpoint>-<cluster>
    if (templ.size() > 1 && templ.first() == QLatin1String("$address.ext"))
    {
        endpoint = templ.at(1).toUInt(&ok, 0);

        if (ok && templ.size() > 2)
        {
            clusterId = templ.at(2).toUInt(&ok, 0);
        }
    }

    if (ok)
    {
        return generateUniqueId(extAddress, endpoint, clusterId);
    }

    return {};
}

/*! Creates and initialises sub-device Resources and ResourceItems if not already present.

    This function can replace database and joining device initialisation.
 */
static bool DEV_InitDeviceFromDescription(Device *device, const DeviceDescription &description)
{
    Q_ASSERT(device);
    Q_ASSERT(description.isValid());

    for (const auto &sub : description.subDevices)
    {
        Q_ASSERT(sub.isValid());

        const auto uniqueId = uniqueIdFromTemplate(sub.uniqueId, device->item(RAttrExtAddress)->toNumber());

        Resource *rsub = nullptr;

        for (auto *r : device->subDevices())
        {
            if (r->item(RAttrUniqueId)->toString() == uniqueId)
            {
                rsub = r; // already existing Resource* for sub-device
                break;
            }
        }

        if (!rsub && sub.endpoint == QLatin1String("/sensors"))
        {

        }
        else if (!rsub && sub.endpoint == QLatin1String("/lights"))
        {

        }

        if (rsub)
        {
            for (const auto &i : sub.items)
            {
                Q_ASSERT(i.isValid());

                auto *item = rsub->item(i.descriptor.suffix);

                if (item)
                {
                    DBG_Printf(DBG_INFO, "sub-device: %s, has item: %s\n", qPrintable(uniqueId), i.descriptor.suffix);
                }
                else
                {
                    DBG_Printf(DBG_INFO, "sub-device: %s, create item: %s\n", qPrintable(uniqueId), i.descriptor.suffix);
                }
            }
        }

    }

    return false;
}

void DEV_GetDeviceDescriptionHandler(Device *device, const Event &event)
{
    if (event.what() == REventStateEnter)
    {
        const auto modelId = device->item(RAttrModelId)->toString();
        const auto description = DeviceDescriptions::instance()->get(device);

        if (description.isValid())
        {
            DBG_Printf(DBG_INFO, "found device description for 0x%016llX, modelId: %s\n", device->key(), qPrintable(modelId));

            DEV_InitDeviceFromDescription(device, description);
            device->setState(DEV_IdleStateHandler); // TODO
        }
        else
        {
            DBG_Printf(DBG_INFO, "No device description for 0x%016llX, modelId: %s\n", device->key(), qPrintable(modelId));
            device->setState(DEV_IdleStateHandler);
        }
    }
}

Device::Device(DeviceKey key, QObject *parent) :
    QObject(parent),
    Resource(RDevices),
    m_deviceKey(key)
{
    addItem(DataTypeBool, RConfigReachable);
    addItem(DataTypeUInt64, RAttrExtAddress);
    addItem(DataTypeUInt16, RAttrNwkAddress);
    addItem(DataTypeString, RAttrUniqueId)->setValue(generateUniqueId(key, 0, 0));
    addItem(DataTypeString, RAttrModelId);

    setState(DEV_InitStateHandler);

    static int initTimer = 1000;
    startStateTimer(initTimer);
    initTimer += 300; // hack for the first round init
}

void Device::addSubDevice(const Resource *sub)
{
    Q_ASSERT(sub);
    Q_ASSERT(sub->item(RAttrUniqueId));
    const auto uniqueId = sub->item(RAttrUniqueId)->toString();

    for (const auto &s : m_subDevices)
    {
        if (std::get<0>(s) == uniqueId)
            return; // already registered
    }

    m_subDevices.push_back({uniqueId, sub->prefix()});
}

void Device::handleEvent(const Event &event)
{
    if (event.what() == REventAwake)
    {
        m_awake.start();
    }

    m_state(this, event);
}

void Device::setState(DeviceStateHandler state)
{
    if (m_state != state)
    {
        if (m_state)
        {
            m_state(this, Event(prefix(), REventStateLeave, 0, key()));
        }
        m_state = state;
        if (m_state)
        {
            // invoke the handler in the next event loop iteration
            plugin()->enqueueEvent(Event(prefix(), REventStateEnter, 0, key()));
        }
    }
}

void Device::startStateTimer(int IntervalMs)
{
    m_timer.start(IntervalMs, this);
}

void Device::stopStateTimer()
{
    m_timer.stop();
}

void Device::timerEvent(QTimerEvent *event)
{
    if (event->timerId() == m_timer.timerId())
    {
        m_timer.stop(); // single shot
        m_state(this, Event(prefix(), REventStateTimeout, 0, key()));
    }
}

qint64 Device::lastAwakeMs() const
{
    return m_awake.isValid() ? m_awake.elapsed() : 8640000;
}

bool Device::reachable() const
{
    if (m_awake.isValid() && m_awake.elapsed() < MinMacPollRxOn)
    {
        return true;
    }
    else if (m_node && !m_node->nodeDescriptor().isNull() && m_node->nodeDescriptor().receiverOnWhenIdle())
    {
        return item(RConfigReachable)->toBool();
    }

    return false;
}

std::vector<Resource *> Device::subDevices() const
{
    std::vector<Resource *> result;

    // temp hack to get valid sub device pointers
    for (const auto &sub : m_subDevices)
    {
        auto *r = plugin()->getResource(std::get<1>(sub), std::get<0>(sub));

        if (r)
        {
            result.push_back(r);
        }
    }

    return result;
}

DeRestPluginPrivate *Device::plugin() const
{
    auto *plugin = dynamic_cast<DeRestPluginPrivate*>(parent());
    Q_ASSERT(plugin);
    return plugin;
}

Device *getOrCreateDevice(QObject *parent, DeviceContainer &devices, DeviceKey key)
{
    Q_ASSERT(key != 0);
    auto d = devices.find(key);

    if (d == devices.end())
    {
        auto res = devices.insert({key, new Device(key, parent)});
        d = res.first;
        Q_ASSERT(d->second);
    }

    Q_ASSERT(d != devices.end());

    return d->second;
}