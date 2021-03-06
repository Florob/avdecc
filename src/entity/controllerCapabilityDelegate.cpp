/*
* Copyright (C) 2016-2019, L-Acoustics and its contributors

* This file is part of LA_avdecc.

* LA_avdecc is free software: you can redistribute it and/or modify
* it under the terms of the GNU Lesser General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.

* LA_avdecc is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU Lesser General Public License for more details.

* You should have received a copy of the GNU Lesser General Public License
* along with LA_avdecc.  If not, see <http://www.gnu.org/licenses/>.
*/

/**
* @file controllerCapabilityDelegate.cpp
* @author Christophe Calmejane
*/

#include "la/avdecc/utils.hpp"
#include "controllerCapabilityDelegate.hpp"
#include "protocol/protocolAemPayloads.hpp"
#include "protocol/protocolMvuPayloads.hpp"
#include <exception>
#include <chrono>
#include <thread>

namespace la
{
namespace avdecc
{
namespace entity
{
namespace controller
{
/* ************************************************************************** */
/* Constants                                                                  */
/* ************************************************************************** */
constexpr int DISCOVER_SEND_DELAY = 10000; // Delay (in milliseconds) between 2 DISCOVER message broadcast

/* ************************************************************************** */
/* Static variables used for bindings                                         */
/* ************************************************************************** */
static model::AudioMappings const s_emptyMappings{}; // Empty AudioMappings used by timeout callback (needs a ref to an AudioMappings)
static model::StreamInfo const s_emptyStreamInfo{}; // Empty StreamInfo used by timeout callback (needs a ref to a StreamInfo)
static model::AvbInfo const s_emptyAvbInfo{}; // Empty AvbInfo used by timeout callback (needs a ref to an AvbInfo)
static model::AsPath const s_emptyAsPath{}; // Empty AsPath used by timeout callback (needs a ref to an AsPath)
static model::AvdeccFixedString const s_emptyAvdeccFixedString{}; // Empty AvdeccFixedString used by timeout callback (needs a ref to a std::string)
static model::MilanInfo const s_emptyMilanInfo{}; // Empty MilanInfo used by timeout callback (need a ref to a MilanInfo)

/* ************************************************************************** */
/* Exceptions                                                                 */
/* ************************************************************************** */
class InvalidDescriptorTypeException final : public Exception
{
public:
	InvalidDescriptorTypeException()
		: Exception("Invalid DescriptorType")
	{
	}
};

/* ************************************************************************** */
/* CapabilityDelegate life cycle                                              */
/* ************************************************************************** */
CapabilityDelegate::CapabilityDelegate(protocol::ProtocolInterface* const protocolInterface, controller::Delegate* controllerDelegate, Interface& controllerInterface, UniqueIdentifier const controllerID) noexcept
	: _protocolInterface(protocolInterface)
	, _controllerDelegate(controllerDelegate)
	, _controllerInterface(controllerInterface)
	, _controllerID(controllerID)
{
	// Create the discovery thread
	_discoveryThread = std::thread(
		[this]
		{
			utils::setCurrentThreadName("avdecc::ControllerDiscovery");
			while (!_shouldTerminate)
			{
				// Request a discovery
				_protocolInterface->discoverRemoteEntities();

				// Wait few seconds before sending another one
				std::chrono::time_point<std::chrono::system_clock> start{ std::chrono::system_clock::now() };
				do
				{
					// Wait a little bit so we don't burn the CPU
					std::this_thread::sleep_for(std::chrono::milliseconds(10));
				} while (!_shouldTerminate && std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now() - start).count() <= DISCOVER_SEND_DELAY);
			}
		});
}

CapabilityDelegate::~CapabilityDelegate() noexcept
{
	// Notify the thread we are shutting down
	_shouldTerminate = true;

	// Wait for the thread to complete its pending tasks
	if (_discoveryThread.joinable())
		_discoveryThread.join();
}

/* ************************************************************************** */
/* Controller methods                                                         */
/* ************************************************************************** */
/* Discovery Protocol (ADP) */
/* Enumeration and Control Protocol (AECP) AEM */
void CapabilityDelegate::acquireEntity(UniqueIdentifier const targetEntityID, bool const isPersistent, model::DescriptorType const descriptorType, model::DescriptorIndex const descriptorIndex, Interface::AcquireEntityHandler const& handler) const noexcept
{
	try
	{
		auto const ser = protocol::aemPayload::serializeAcquireEntityCommand(isPersistent ? protocol::AemAcquireEntityFlags::Persistent : protocol::AemAcquireEntityFlags::None, UniqueIdentifier::getNullUniqueIdentifier(), descriptorType, descriptorIndex);
		auto const errorCallback = LocalEntityImpl<>::makeAemAECPErrorHandler(handler, &_controllerInterface, targetEntityID, std::placeholders::_1, UniqueIdentifier::getNullUniqueIdentifier(), descriptorType, descriptorIndex);

		sendAemAecpCommand(targetEntityID, protocol::AemCommandType::AcquireEntity, ser.data(), ser.size(), errorCallback, handler);
	}
	catch ([[maybe_unused]] std::exception const& e)
	{
		LOG_CONTROLLER_ENTITY_DEBUG(targetEntityID, "Failed to serialize acquireEntity: {}", e.what());
	}
}

void CapabilityDelegate::releaseEntity(UniqueIdentifier const targetEntityID, model::DescriptorType const descriptorType, model::DescriptorIndex const descriptorIndex, Interface::ReleaseEntityHandler const& handler) const noexcept
{
	try
	{
		auto const ser = protocol::aemPayload::serializeAcquireEntityCommand(protocol::AemAcquireEntityFlags::Release, UniqueIdentifier::getNullUniqueIdentifier(), descriptorType, descriptorIndex);
		auto const errorCallback = LocalEntityImpl<>::makeAemAECPErrorHandler(handler, &_controllerInterface, targetEntityID, std::placeholders::_1, UniqueIdentifier::getNullUniqueIdentifier(), descriptorType, descriptorIndex);

		sendAemAecpCommand(targetEntityID, protocol::AemCommandType::AcquireEntity, ser.data(), ser.size(), errorCallback, handler);
	}
	catch ([[maybe_unused]] std::exception const& e)
	{
		LOG_CONTROLLER_ENTITY_DEBUG(targetEntityID, "Failed to serialize releaseEntity: {}", e.what());
	}
}

void CapabilityDelegate::lockEntity(UniqueIdentifier const targetEntityID, model::DescriptorType const descriptorType, model::DescriptorIndex const descriptorIndex, Interface::LockEntityHandler const& handler) const noexcept
{
	try
	{
		auto const ser = protocol::aemPayload::serializeLockEntityCommand(protocol::AemLockEntityFlags::None, UniqueIdentifier::getNullUniqueIdentifier(), descriptorType, descriptorIndex);
		auto const errorCallback = LocalEntityImpl<>::makeAemAECPErrorHandler(handler, &_controllerInterface, targetEntityID, std::placeholders::_1, UniqueIdentifier::getNullUniqueIdentifier(), descriptorType, descriptorIndex);

		sendAemAecpCommand(targetEntityID, protocol::AemCommandType::LockEntity, ser.data(), ser.size(), errorCallback, handler);
	}
	catch ([[maybe_unused]] std::exception const& e)
	{
		LOG_CONTROLLER_ENTITY_DEBUG(targetEntityID, "Failed to serialize lockEntity: {}", e.what());
	}
}

void CapabilityDelegate::unlockEntity(UniqueIdentifier const targetEntityID, model::DescriptorType const descriptorType, model::DescriptorIndex const descriptorIndex, Interface::UnlockEntityHandler const& handler) const noexcept
{
	try
	{
		auto const ser = protocol::aemPayload::serializeLockEntityCommand(protocol::AemLockEntityFlags::Unlock, UniqueIdentifier::getNullUniqueIdentifier(), descriptorType, descriptorIndex);
		auto const errorCallback = LocalEntityImpl<>::makeAemAECPErrorHandler(handler, &_controllerInterface, targetEntityID, std::placeholders::_1, UniqueIdentifier::getNullUniqueIdentifier(), descriptorType, descriptorIndex);

		sendAemAecpCommand(targetEntityID, protocol::AemCommandType::LockEntity, ser.data(), ser.size(), errorCallback, handler);
	}
	catch ([[maybe_unused]] std::exception const& e)
	{
		LOG_CONTROLLER_ENTITY_DEBUG(targetEntityID, "Failed to serialize unlockEntity: {}", e.what());
	}
}

void CapabilityDelegate::queryEntityAvailable(UniqueIdentifier const targetEntityID, Interface::QueryEntityAvailableHandler const& handler) const noexcept
{
	auto const errorCallback = LocalEntityImpl<>::makeAemAECPErrorHandler(handler, &_controllerInterface, targetEntityID, std::placeholders::_1);

	sendAemAecpCommand(targetEntityID, protocol::AemCommandType::EntityAvailable, nullptr, 0, errorCallback, handler);
}

void CapabilityDelegate::queryControllerAvailable(UniqueIdentifier const targetEntityID, Interface::QueryControllerAvailableHandler const& handler) const noexcept
{
	auto const errorCallback = LocalEntityImpl<>::makeAemAECPErrorHandler(handler, &_controllerInterface, targetEntityID, std::placeholders::_1);

	sendAemAecpCommand(targetEntityID, protocol::AemCommandType::ControllerAvailable, nullptr, 0, errorCallback, handler);
}

void CapabilityDelegate::registerUnsolicitedNotifications(UniqueIdentifier const targetEntityID, Interface::RegisterUnsolicitedNotificationsHandler const& handler) const noexcept
{
	auto errorCallback = LocalEntityImpl<>::makeAemAECPErrorHandler(handler, &_controllerInterface, targetEntityID, std::placeholders::_1);

	sendAemAecpCommand(targetEntityID, protocol::AemCommandType::RegisterUnsolicitedNotification, nullptr, 0, errorCallback, handler);
}

void CapabilityDelegate::unregisterUnsolicitedNotifications(UniqueIdentifier const targetEntityID, Interface::UnregisterUnsolicitedNotificationsHandler const& handler) const noexcept
{
	auto errorCallback = LocalEntityImpl<>::makeAemAECPErrorHandler(handler, &_controllerInterface, targetEntityID, std::placeholders::_1);

	sendAemAecpCommand(targetEntityID, protocol::AemCommandType::DeregisterUnsolicitedNotification, nullptr, 0, errorCallback, handler);
}

void CapabilityDelegate::readEntityDescriptor(UniqueIdentifier const targetEntityID, Interface::EntityDescriptorHandler const& handler) const noexcept
{
	try
	{
		auto const ser = protocol::aemPayload::serializeReadDescriptorCommand(model::ConfigurationIndex(0u), model::DescriptorType::Entity, model::DescriptorIndex(0u));
		auto const errorCallback = LocalEntityImpl<>::makeAemAECPErrorHandler(handler, &_controllerInterface, targetEntityID, std::placeholders::_1, model::EntityDescriptor{});

		sendAemAecpCommand(targetEntityID, protocol::AemCommandType::ReadDescriptor, ser.data(), ser.size(), errorCallback, handler);
	}
	catch ([[maybe_unused]] std::exception const& e)
	{
		LOG_CONTROLLER_ENTITY_DEBUG(targetEntityID, "Failed to serialize readEntityDescriptor: {}", e.what());
	}
}

void CapabilityDelegate::readConfigurationDescriptor(UniqueIdentifier const targetEntityID, model::ConfigurationIndex const configurationIndex, Interface::ConfigurationDescriptorHandler const& handler) const noexcept
{
	try
	{
		auto const ser = protocol::aemPayload::serializeReadDescriptorCommand(model::ConfigurationIndex(0u), model::DescriptorType::Configuration, static_cast<model::DescriptorIndex>(configurationIndex)); // Passing configurationIndex as a DescriptorIndex is NOT an error. See 7.4.5.1
		auto const errorCallback = LocalEntityImpl<>::makeAemAECPErrorHandler(handler, &_controllerInterface, targetEntityID, std::placeholders::_1, configurationIndex, model::ConfigurationDescriptor{});

		sendAemAecpCommand(targetEntityID, protocol::AemCommandType::ReadDescriptor, ser.data(), ser.size(), errorCallback, handler);
	}
	catch ([[maybe_unused]] std::exception const& e)
	{
		LOG_CONTROLLER_ENTITY_DEBUG(targetEntityID, "Failed to serialize readConfigurationDescriptor: {}", e.what());
	}
}

void CapabilityDelegate::readAudioUnitDescriptor(UniqueIdentifier const targetEntityID, model::ConfigurationIndex const configurationIndex, model::AudioUnitIndex const audioUnitIndex, Interface::AudioUnitDescriptorHandler const& handler) const noexcept
{
	try
	{
		auto const ser = protocol::aemPayload::serializeReadDescriptorCommand(configurationIndex, model::DescriptorType::AudioUnit, audioUnitIndex);
		auto const errorCallback = LocalEntityImpl<>::makeAemAECPErrorHandler(handler, &_controllerInterface, targetEntityID, std::placeholders::_1, configurationIndex, audioUnitIndex, model::AudioUnitDescriptor{});

		sendAemAecpCommand(targetEntityID, protocol::AemCommandType::ReadDescriptor, ser.data(), ser.size(), errorCallback, handler);
	}
	catch ([[maybe_unused]] std::exception const& e)
	{
		LOG_CONTROLLER_ENTITY_DEBUG(targetEntityID, "Failed to serialize readAudioUnitDescriptor: {}", e.what());
	}
}

void CapabilityDelegate::readStreamInputDescriptor(UniqueIdentifier const targetEntityID, model::ConfigurationIndex const configurationIndex, model::StreamIndex const streamIndex, Interface::StreamInputDescriptorHandler const& handler) const noexcept
{
	try
	{
		auto const ser = protocol::aemPayload::serializeReadDescriptorCommand(configurationIndex, model::DescriptorType::StreamInput, streamIndex);
		auto const errorCallback = LocalEntityImpl<>::makeAemAECPErrorHandler(handler, &_controllerInterface, targetEntityID, std::placeholders::_1, configurationIndex, streamIndex, model::StreamDescriptor{});

		sendAemAecpCommand(targetEntityID, protocol::AemCommandType::ReadDescriptor, ser.data(), ser.size(), errorCallback, handler);
	}
	catch ([[maybe_unused]] std::exception const& e)
	{
		LOG_CONTROLLER_ENTITY_DEBUG(targetEntityID, "Failed to serialize readStreamInputDescriptor: {}", e.what());
	}
}

void CapabilityDelegate::readStreamOutputDescriptor(UniqueIdentifier const targetEntityID, model::ConfigurationIndex const configurationIndex, model::StreamIndex const streamIndex, Interface::StreamOutputDescriptorHandler const& handler) const noexcept
{
	try
	{
		auto const ser = protocol::aemPayload::serializeReadDescriptorCommand(configurationIndex, model::DescriptorType::StreamOutput, streamIndex);
		auto const errorCallback = LocalEntityImpl<>::makeAemAECPErrorHandler(handler, &_controllerInterface, targetEntityID, std::placeholders::_1, configurationIndex, streamIndex, model::StreamDescriptor{});

		sendAemAecpCommand(targetEntityID, protocol::AemCommandType::ReadDescriptor, ser.data(), ser.size(), errorCallback, handler);
	}
	catch ([[maybe_unused]] std::exception const& e)
	{
		LOG_CONTROLLER_ENTITY_DEBUG(targetEntityID, "Failed to serialize readStreamOutputDescriptor: {}", e.what());
	}
}

void CapabilityDelegate::readJackInputDescriptor(UniqueIdentifier const targetEntityID, model::ConfigurationIndex const configurationIndex, model::JackIndex const jackIndex, Interface::JackInputDescriptorHandler const& handler) const noexcept
{
	try
	{
		auto const ser = protocol::aemPayload::serializeReadDescriptorCommand(configurationIndex, model::DescriptorType::JackInput, jackIndex);
		auto const errorCallback = LocalEntityImpl<>::makeAemAECPErrorHandler(handler, &_controllerInterface, targetEntityID, std::placeholders::_1, configurationIndex, jackIndex, model::JackDescriptor{});

		sendAemAecpCommand(targetEntityID, protocol::AemCommandType::ReadDescriptor, ser.data(), ser.size(), errorCallback, handler);
	}
	catch ([[maybe_unused]] std::exception const& e)
	{
		LOG_CONTROLLER_ENTITY_DEBUG(targetEntityID, "Failed to serialize readJackInputDescriptor: {}", e.what());
	}
}

void CapabilityDelegate::readJackOutputDescriptor(UniqueIdentifier const targetEntityID, model::ConfigurationIndex const configurationIndex, model::JackIndex const jackIndex, Interface::JackOutputDescriptorHandler const& handler) const noexcept
{
	try
	{
		auto const ser = protocol::aemPayload::serializeReadDescriptorCommand(configurationIndex, model::DescriptorType::JackOutput, jackIndex);
		auto const errorCallback = LocalEntityImpl<>::makeAemAECPErrorHandler(handler, &_controllerInterface, targetEntityID, std::placeholders::_1, configurationIndex, jackIndex, model::JackDescriptor{});

		sendAemAecpCommand(targetEntityID, protocol::AemCommandType::ReadDescriptor, ser.data(), ser.size(), errorCallback, handler);
	}
	catch ([[maybe_unused]] std::exception const& e)
	{
		LOG_CONTROLLER_ENTITY_DEBUG(targetEntityID, "Failed to serialize readJackOutputDescriptor: {}", e.what());
	}
}

void CapabilityDelegate::readAvbInterfaceDescriptor(UniqueIdentifier const targetEntityID, model::ConfigurationIndex const configurationIndex, model::AvbInterfaceIndex const avbInterfaceIndex, Interface::AvbInterfaceDescriptorHandler const& handler) const noexcept
{
	try
	{
		auto const ser = protocol::aemPayload::serializeReadDescriptorCommand(configurationIndex, model::DescriptorType::AvbInterface, avbInterfaceIndex);
		auto const errorCallback = LocalEntityImpl<>::makeAemAECPErrorHandler(handler, &_controllerInterface, targetEntityID, std::placeholders::_1, configurationIndex, avbInterfaceIndex, model::AvbInterfaceDescriptor{});

		sendAemAecpCommand(targetEntityID, protocol::AemCommandType::ReadDescriptor, ser.data(), ser.size(), errorCallback, handler);
	}
	catch ([[maybe_unused]] std::exception const& e)
	{
		LOG_CONTROLLER_ENTITY_DEBUG(targetEntityID, "Failed to serialize readAvbInterfaceDescriptor: {}", e.what());
	}
}

void CapabilityDelegate::readClockSourceDescriptor(UniqueIdentifier const targetEntityID, model::ConfigurationIndex const configurationIndex, model::ClockSourceIndex const clockSourceIndex, Interface::ClockSourceDescriptorHandler const& handler) const noexcept
{
	try
	{
		auto const ser = protocol::aemPayload::serializeReadDescriptorCommand(configurationIndex, model::DescriptorType::ClockSource, clockSourceIndex);
		auto const errorCallback = LocalEntityImpl<>::makeAemAECPErrorHandler(handler, &_controllerInterface, targetEntityID, std::placeholders::_1, configurationIndex, clockSourceIndex, model::ClockSourceDescriptor{});

		sendAemAecpCommand(targetEntityID, protocol::AemCommandType::ReadDescriptor, ser.data(), ser.size(), errorCallback, handler);
	}
	catch ([[maybe_unused]] std::exception const& e)
	{
		LOG_CONTROLLER_ENTITY_DEBUG(targetEntityID, "Failed to serialize readClockSourceDescriptor: '}", e.what());
	}
}

void CapabilityDelegate::readMemoryObjectDescriptor(UniqueIdentifier const targetEntityID, model::ConfigurationIndex const configurationIndex, model::MemoryObjectIndex const memoryObjectIndex, Interface::MemoryObjectDescriptorHandler const& handler) const noexcept
{
	try
	{
		auto const ser = protocol::aemPayload::serializeReadDescriptorCommand(configurationIndex, model::DescriptorType::MemoryObject, memoryObjectIndex);
		auto const errorCallback = LocalEntityImpl<>::makeAemAECPErrorHandler(handler, &_controllerInterface, targetEntityID, std::placeholders::_1, configurationIndex, memoryObjectIndex, model::MemoryObjectDescriptor{});

		sendAemAecpCommand(targetEntityID, protocol::AemCommandType::ReadDescriptor, ser.data(), ser.size(), errorCallback, handler);
	}
	catch ([[maybe_unused]] std::exception const& e)
	{
		LOG_CONTROLLER_ENTITY_DEBUG(targetEntityID, "Failed to serialize readMemoryObjectDescriptor: {}", e.what());
	}
}

void CapabilityDelegate::readLocaleDescriptor(UniqueIdentifier const targetEntityID, model::ConfigurationIndex const configurationIndex, model::LocaleIndex const localeIndex, Interface::LocaleDescriptorHandler const& handler) const noexcept
{
	try
	{
		auto const ser = protocol::aemPayload::serializeReadDescriptorCommand(configurationIndex, model::DescriptorType::Locale, localeIndex);
		auto const errorCallback = LocalEntityImpl<>::makeAemAECPErrorHandler(handler, &_controllerInterface, targetEntityID, std::placeholders::_1, configurationIndex, localeIndex, model::LocaleDescriptor{});

		sendAemAecpCommand(targetEntityID, protocol::AemCommandType::ReadDescriptor, ser.data(), ser.size(), errorCallback, handler);
	}
	catch ([[maybe_unused]] std::exception const& e)
	{
		LOG_CONTROLLER_ENTITY_DEBUG(targetEntityID, "Failed to serialize readLocaleDescriptor: {}", e.what());
	}
}

void CapabilityDelegate::readStringsDescriptor(UniqueIdentifier const targetEntityID, model::ConfigurationIndex const configurationIndex, model::StringsIndex const stringsIndex, Interface::StringsDescriptorHandler const& handler) const noexcept
{
	try
	{
		auto const ser = protocol::aemPayload::serializeReadDescriptorCommand(configurationIndex, model::DescriptorType::Strings, stringsIndex);
		auto const errorCallback = LocalEntityImpl<>::makeAemAECPErrorHandler(handler, &_controllerInterface, targetEntityID, std::placeholders::_1, configurationIndex, stringsIndex, model::StringsDescriptor{});

		sendAemAecpCommand(targetEntityID, protocol::AemCommandType::ReadDescriptor, ser.data(), ser.size(), errorCallback, handler);
	}
	catch ([[maybe_unused]] std::exception const& e)
	{
		LOG_CONTROLLER_ENTITY_DEBUG(targetEntityID, "Failed to serialize readStringsDescriptor: {}", e.what());
	}
}

void CapabilityDelegate::readStreamPortInputDescriptor(UniqueIdentifier const targetEntityID, model::ConfigurationIndex const configurationIndex, model::StreamPortIndex const streamPortIndex, Interface::StreamPortInputDescriptorHandler const& handler) const noexcept
{
	try
	{
		auto const ser = protocol::aemPayload::serializeReadDescriptorCommand(configurationIndex, model::DescriptorType::StreamPortInput, streamPortIndex);
		auto const errorCallback = LocalEntityImpl<>::makeAemAECPErrorHandler(handler, &_controllerInterface, targetEntityID, std::placeholders::_1, configurationIndex, streamPortIndex, model::StreamPortDescriptor{});

		sendAemAecpCommand(targetEntityID, protocol::AemCommandType::ReadDescriptor, ser.data(), ser.size(), errorCallback, handler);
	}
	catch ([[maybe_unused]] std::exception const& e)
	{
		LOG_CONTROLLER_ENTITY_DEBUG(targetEntityID, "Failed to serialize readStreamPortInputDescriptor: {}", e.what());
	}
}

void CapabilityDelegate::readStreamPortOutputDescriptor(UniqueIdentifier const targetEntityID, model::ConfigurationIndex const configurationIndex, model::StreamPortIndex const streamPortIndex, Interface::StreamPortOutputDescriptorHandler const& handler) const noexcept
{
	try
	{
		auto const ser = protocol::aemPayload::serializeReadDescriptorCommand(configurationIndex, model::DescriptorType::StreamPortOutput, streamPortIndex);
		auto const errorCallback = LocalEntityImpl<>::makeAemAECPErrorHandler(handler, &_controllerInterface, targetEntityID, std::placeholders::_1, configurationIndex, streamPortIndex, model::StreamPortDescriptor{});

		sendAemAecpCommand(targetEntityID, protocol::AemCommandType::ReadDescriptor, ser.data(), ser.size(), errorCallback, handler);
	}
	catch ([[maybe_unused]] std::exception const& e)
	{
		LOG_CONTROLLER_ENTITY_DEBUG(targetEntityID, "Failed to serialize readStreamPortOutputDescriptor: {}", e.what());
	}
}

void CapabilityDelegate::readExternalPortInputDescriptor(UniqueIdentifier const targetEntityID, model::ConfigurationIndex const configurationIndex, model::ExternalPortIndex const externalPortIndex, Interface::ExternalPortInputDescriptorHandler const& handler) const noexcept
{
	try
	{
		auto const ser = protocol::aemPayload::serializeReadDescriptorCommand(configurationIndex, model::DescriptorType::ExternalPortInput, externalPortIndex);
		auto const errorCallback = LocalEntityImpl<>::makeAemAECPErrorHandler(handler, &_controllerInterface, targetEntityID, std::placeholders::_1, configurationIndex, externalPortIndex, model::ExternalPortDescriptor{});

		sendAemAecpCommand(targetEntityID, protocol::AemCommandType::ReadDescriptor, ser.data(), ser.size(), errorCallback, handler);
	}
	catch ([[maybe_unused]] std::exception const& e)
	{
		LOG_CONTROLLER_ENTITY_DEBUG(targetEntityID, "Failed to serialize readExternalPortInputDescriptor: {}", e.what());
	}
}

void CapabilityDelegate::readExternalPortOutputDescriptor(UniqueIdentifier const targetEntityID, model::ConfigurationIndex const configurationIndex, model::ExternalPortIndex const externalPortIndex, Interface::ExternalPortOutputDescriptorHandler const& handler) const noexcept
{
	try
	{
		auto const ser = protocol::aemPayload::serializeReadDescriptorCommand(configurationIndex, model::DescriptorType::ExternalPortOutput, externalPortIndex);
		auto const errorCallback = LocalEntityImpl<>::makeAemAECPErrorHandler(handler, &_controllerInterface, targetEntityID, std::placeholders::_1, configurationIndex, externalPortIndex, model::ExternalPortDescriptor{});

		sendAemAecpCommand(targetEntityID, protocol::AemCommandType::ReadDescriptor, ser.data(), ser.size(), errorCallback, handler);
	}
	catch ([[maybe_unused]] std::exception const& e)
	{
		LOG_CONTROLLER_ENTITY_DEBUG(targetEntityID, "Failed to serialize readExternalPortInputDescriptor: {}", e.what());
	}
}

void CapabilityDelegate::readInternalPortInputDescriptor(UniqueIdentifier const targetEntityID, model::ConfigurationIndex const configurationIndex, model::InternalPortIndex const internalPortIndex, Interface::InternalPortInputDescriptorHandler const& handler) const noexcept
{
	try
	{
		auto const ser = protocol::aemPayload::serializeReadDescriptorCommand(configurationIndex, model::DescriptorType::InternalPortInput, internalPortIndex);
		auto const errorCallback = LocalEntityImpl<>::makeAemAECPErrorHandler(handler, &_controllerInterface, targetEntityID, std::placeholders::_1, configurationIndex, internalPortIndex, model::InternalPortDescriptor{});

		sendAemAecpCommand(targetEntityID, protocol::AemCommandType::ReadDescriptor, ser.data(), ser.size(), errorCallback, handler);
	}
	catch ([[maybe_unused]] std::exception const& e)
	{
		LOG_CONTROLLER_ENTITY_DEBUG(targetEntityID, "Failed to serialize readInternalPortInputDescriptor: {}", e.what());
	}
}

void CapabilityDelegate::readInternalPortOutputDescriptor(UniqueIdentifier const targetEntityID, model::ConfigurationIndex const configurationIndex, model::InternalPortIndex const internalPortIndex, Interface::InternalPortOutputDescriptorHandler const& handler) const noexcept
{
	try
	{
		auto const ser = protocol::aemPayload::serializeReadDescriptorCommand(configurationIndex, model::DescriptorType::InternalPortOutput, internalPortIndex);
		auto const errorCallback = LocalEntityImpl<>::makeAemAECPErrorHandler(handler, &_controllerInterface, targetEntityID, std::placeholders::_1, configurationIndex, internalPortIndex, model::InternalPortDescriptor{});

		sendAemAecpCommand(targetEntityID, protocol::AemCommandType::ReadDescriptor, ser.data(), ser.size(), errorCallback, handler);
	}
	catch ([[maybe_unused]] std::exception const& e)
	{
		LOG_CONTROLLER_ENTITY_DEBUG(targetEntityID, "Failed to serialize readInternalPortOutputDescriptor: {}", e.what());
	}
}

void CapabilityDelegate::readAudioClusterDescriptor(UniqueIdentifier const targetEntityID, model::ConfigurationIndex const configurationIndex, model::ClusterIndex const clusterIndex, Interface::AudioClusterDescriptorHandler const& handler) const noexcept
{
	try
	{
		auto const ser = protocol::aemPayload::serializeReadDescriptorCommand(configurationIndex, model::DescriptorType::AudioCluster, clusterIndex);
		auto const errorCallback = LocalEntityImpl<>::makeAemAECPErrorHandler(handler, &_controllerInterface, targetEntityID, std::placeholders::_1, configurationIndex, clusterIndex, model::AudioClusterDescriptor{});

		sendAemAecpCommand(targetEntityID, protocol::AemCommandType::ReadDescriptor, ser.data(), ser.size(), errorCallback, handler);
	}
	catch ([[maybe_unused]] std::exception const& e)
	{
		LOG_CONTROLLER_ENTITY_DEBUG(targetEntityID, "Failed to serialize readAudioClusterDescriptor: {}", e.what());
	}
}

void CapabilityDelegate::readAudioMapDescriptor(UniqueIdentifier const targetEntityID, model::ConfigurationIndex const configurationIndex, model::MapIndex const mapIndex, Interface::AudioMapDescriptorHandler const& handler) const noexcept
{
	try
	{
		auto const ser = protocol::aemPayload::serializeReadDescriptorCommand(configurationIndex, model::DescriptorType::AudioMap, mapIndex);
		auto const errorCallback = LocalEntityImpl<>::makeAemAECPErrorHandler(handler, &_controllerInterface, targetEntityID, std::placeholders::_1, configurationIndex, mapIndex, model::AudioMapDescriptor{});

		sendAemAecpCommand(targetEntityID, protocol::AemCommandType::ReadDescriptor, ser.data(), ser.size(), errorCallback, handler);
	}
	catch ([[maybe_unused]] std::exception const& e)
	{
		LOG_CONTROLLER_ENTITY_DEBUG(targetEntityID, "Failed to serialize readAudioMapDescriptor: {}", e.what());
	}
}

void CapabilityDelegate::readClockDomainDescriptor(UniqueIdentifier const targetEntityID, model::ConfigurationIndex const configurationIndex, model::ClockDomainIndex const clockDomainIndex, Interface::ClockDomainDescriptorHandler const& handler) const noexcept
{
	try
	{
		auto const ser = protocol::aemPayload::serializeReadDescriptorCommand(configurationIndex, model::DescriptorType::ClockDomain, clockDomainIndex);
		auto const errorCallback = LocalEntityImpl<>::makeAemAECPErrorHandler(handler, &_controllerInterface, targetEntityID, std::placeholders::_1, configurationIndex, clockDomainIndex, model::ClockDomainDescriptor{});

		sendAemAecpCommand(targetEntityID, protocol::AemCommandType::ReadDescriptor, ser.data(), ser.size(), errorCallback, handler);
	}
	catch ([[maybe_unused]] std::exception const& e)
	{
		LOG_CONTROLLER_ENTITY_DEBUG(targetEntityID, "Failed to serialize readClockDomainDescriptor: {}", e.what());
	}
}

void CapabilityDelegate::setConfiguration(UniqueIdentifier const targetEntityID, model::ConfigurationIndex const configurationIndex, Interface::SetConfigurationHandler const& handler) const noexcept
{
	try
	{
		auto const ser = protocol::aemPayload::serializeSetConfigurationCommand(configurationIndex);
		auto const errorCallback = LocalEntityImpl<>::makeAemAECPErrorHandler(handler, &_controllerInterface, targetEntityID, std::placeholders::_1, configurationIndex);

		sendAemAecpCommand(targetEntityID, protocol::AemCommandType::SetConfiguration, ser.data(), ser.size(), errorCallback, handler);
	}
	catch ([[maybe_unused]] std::exception const& e)
	{
		LOG_CONTROLLER_ENTITY_DEBUG(targetEntityID, "Failed to serialize setConfiguration: {}", e.what());
	}
}

void CapabilityDelegate::setStreamInputFormat(UniqueIdentifier const targetEntityID, model::StreamIndex const streamIndex, model::StreamFormat const streamFormat, Interface::SetStreamInputFormatHandler const& handler) const noexcept
{
	try
	{
		auto const ser = protocol::aemPayload::serializeSetStreamFormatCommand(model::DescriptorType::StreamInput, streamIndex, streamFormat);
		auto const errorCallback = LocalEntityImpl<>::makeAemAECPErrorHandler(handler, &_controllerInterface, targetEntityID, std::placeholders::_1, streamIndex, model::StreamFormat());

		sendAemAecpCommand(targetEntityID, protocol::AemCommandType::SetStreamFormat, ser.data(), ser.size(), errorCallback, handler);
	}
	catch ([[maybe_unused]] std::exception const& e)
	{
		LOG_CONTROLLER_ENTITY_DEBUG(targetEntityID, "Failed to serialize setStreamInputFormat: {}", e.what());
	}
}

void CapabilityDelegate::getStreamInputFormat(UniqueIdentifier const targetEntityID, model::StreamIndex const streamIndex, Interface::GetStreamInputFormatHandler const& handler) const noexcept
{
	try
	{
		auto const ser = protocol::aemPayload::serializeGetStreamFormatCommand(model::DescriptorType::StreamInput, streamIndex);
		auto const errorCallback = LocalEntityImpl<>::makeAemAECPErrorHandler(handler, &_controllerInterface, targetEntityID, std::placeholders::_1, streamIndex, model::StreamFormat());

		sendAemAecpCommand(targetEntityID, protocol::AemCommandType::GetStreamFormat, ser.data(), ser.size(), errorCallback, handler);
	}
	catch ([[maybe_unused]] std::exception const& e)
	{
		LOG_CONTROLLER_ENTITY_DEBUG(targetEntityID, "Failed to serialize getStreamInputFormat: {}", e.what());
	}
}

void CapabilityDelegate::setStreamOutputFormat(UniqueIdentifier const targetEntityID, model::StreamIndex const streamIndex, model::StreamFormat const streamFormat, Interface::SetStreamOutputFormatHandler const& handler) const noexcept
{
	try
	{
		auto const ser = protocol::aemPayload::serializeSetStreamFormatCommand(model::DescriptorType::StreamOutput, streamIndex, streamFormat);
		auto const errorCallback = LocalEntityImpl<>::makeAemAECPErrorHandler(handler, &_controllerInterface, targetEntityID, std::placeholders::_1, streamIndex, model::StreamFormat());

		sendAemAecpCommand(targetEntityID, protocol::AemCommandType::SetStreamFormat, ser.data(), ser.size(), errorCallback, handler);
	}
	catch ([[maybe_unused]] std::exception const& e)
	{
		LOG_CONTROLLER_ENTITY_DEBUG(targetEntityID, "Failed to serialize setStreamOutputFormat: {}", e.what());
	}
}

void CapabilityDelegate::getStreamOutputFormat(UniqueIdentifier const targetEntityID, model::StreamIndex const streamIndex, Interface::GetStreamOutputFormatHandler const& handler) const noexcept
{
	try
	{
		auto const ser = protocol::aemPayload::serializeGetStreamFormatCommand(model::DescriptorType::StreamOutput, streamIndex);
		auto const errorCallback = LocalEntityImpl<>::makeAemAECPErrorHandler(handler, &_controllerInterface, targetEntityID, std::placeholders::_1, streamIndex, model::StreamFormat());

		sendAemAecpCommand(targetEntityID, protocol::AemCommandType::GetStreamFormat, ser.data(), ser.size(), errorCallback, handler);
	}
	catch ([[maybe_unused]] std::exception const& e)
	{
		LOG_CONTROLLER_ENTITY_DEBUG(targetEntityID, "Failed to serialize getStreamOutputFormat: {}", e.what());
	}
}

void CapabilityDelegate::getStreamPortInputAudioMap(UniqueIdentifier const targetEntityID, model::StreamPortIndex const streamPortIndex, model::MapIndex const mapIndex, Interface::GetStreamPortInputAudioMapHandler const& handler) const noexcept
{
	try
	{
		auto const ser = protocol::aemPayload::serializeGetAudioMapCommand(model::DescriptorType::StreamPortInput, streamPortIndex, mapIndex);
		auto const errorCallback = LocalEntityImpl<>::makeAemAECPErrorHandler(handler, &_controllerInterface, targetEntityID, std::placeholders::_1, streamPortIndex, model::MapIndex(0), mapIndex, s_emptyMappings);

		sendAemAecpCommand(targetEntityID, protocol::AemCommandType::GetAudioMap, ser.data(), ser.size(), errorCallback, handler);
	}
	catch ([[maybe_unused]] std::exception const& e)
	{
		LOG_CONTROLLER_ENTITY_DEBUG(targetEntityID, "Failed to serialize getStreamInputAudioMap: {}", e.what());
	}
}

void CapabilityDelegate::getStreamPortOutputAudioMap(UniqueIdentifier const targetEntityID, model::StreamPortIndex const streamPortIndex, model::MapIndex const mapIndex, Interface::GetStreamPortOutputAudioMapHandler const& handler) const noexcept
{
	try
	{
		auto const ser = protocol::aemPayload::serializeGetAudioMapCommand(model::DescriptorType::StreamPortOutput, streamPortIndex, mapIndex);
		auto const errorCallback = LocalEntityImpl<>::makeAemAECPErrorHandler(handler, &_controllerInterface, targetEntityID, std::placeholders::_1, streamPortIndex, model::MapIndex(0), mapIndex, s_emptyMappings);

		sendAemAecpCommand(targetEntityID, protocol::AemCommandType::GetAudioMap, ser.data(), ser.size(), errorCallback, handler);
	}
	catch ([[maybe_unused]] std::exception const& e)
	{
		LOG_CONTROLLER_ENTITY_DEBUG(targetEntityID, "Failed to serialize getStreamOutputAudioMap: {}", e.what());
	}
}

void CapabilityDelegate::addStreamPortInputAudioMappings(UniqueIdentifier const targetEntityID, model::StreamPortIndex const streamPortIndex, model::AudioMappings const& mappings, Interface::AddStreamPortInputAudioMappingsHandler const& handler) const noexcept
{
	try
	{
		auto const ser = protocol::aemPayload::serializeAddAudioMappingsCommand(model::DescriptorType::StreamPortInput, streamPortIndex, mappings);
		auto const errorCallback = LocalEntityImpl<>::makeAemAECPErrorHandler(handler, &_controllerInterface, targetEntityID, std::placeholders::_1, streamPortIndex, s_emptyMappings);

		sendAemAecpCommand(targetEntityID, protocol::AemCommandType::AddAudioMappings, ser.data(), ser.size(), errorCallback, handler);
	}
	catch ([[maybe_unused]] std::exception const& e)
	{
		LOG_CONTROLLER_ENTITY_DEBUG(targetEntityID, "Failed to serialize addStreamInputAudioMappings: {}", e.what());
	}
}

void CapabilityDelegate::addStreamPortOutputAudioMappings(UniqueIdentifier const targetEntityID, model::StreamPortIndex const streamPortIndex, model::AudioMappings const& mappings, Interface::AddStreamPortOutputAudioMappingsHandler const& handler) const noexcept
{
	try
	{
		auto const ser = protocol::aemPayload::serializeAddAudioMappingsCommand(model::DescriptorType::StreamPortOutput, streamPortIndex, mappings);
		auto const errorCallback = LocalEntityImpl<>::makeAemAECPErrorHandler(handler, &_controllerInterface, targetEntityID, std::placeholders::_1, streamPortIndex, s_emptyMappings);

		sendAemAecpCommand(targetEntityID, protocol::AemCommandType::AddAudioMappings, ser.data(), ser.size(), errorCallback, handler);
	}
	catch ([[maybe_unused]] std::exception const& e)
	{
		LOG_CONTROLLER_ENTITY_DEBUG(targetEntityID, "Failed to serialize addStreamOutputAudioMappings: {}", e.what());
	}
}

void CapabilityDelegate::removeStreamPortInputAudioMappings(UniqueIdentifier const targetEntityID, model::StreamPortIndex const streamPortIndex, model::AudioMappings const& mappings, Interface::RemoveStreamPortInputAudioMappingsHandler const& handler) const noexcept
{
	try
	{
		auto const ser = protocol::aemPayload::serializeRemoveAudioMappingsCommand(model::DescriptorType::StreamPortInput, streamPortIndex, mappings);
		auto const errorCallback = LocalEntityImpl<>::makeAemAECPErrorHandler(handler, &_controllerInterface, targetEntityID, std::placeholders::_1, streamPortIndex, s_emptyMappings);

		sendAemAecpCommand(targetEntityID, protocol::AemCommandType::RemoveAudioMappings, ser.data(), ser.size(), errorCallback, handler);
	}
	catch ([[maybe_unused]] std::exception const& e)
	{
		LOG_CONTROLLER_ENTITY_DEBUG(targetEntityID, "Failed to serialize removeStreamInputAudioMappings: {}", e.what());
	}
}

void CapabilityDelegate::removeStreamPortOutputAudioMappings(UniqueIdentifier const targetEntityID, model::StreamPortIndex const streamPortIndex, model::AudioMappings const& mappings, Interface::RemoveStreamPortOutputAudioMappingsHandler const& handler) const noexcept
{
	try
	{
		auto const ser = protocol::aemPayload::serializeRemoveAudioMappingsCommand(model::DescriptorType::StreamPortOutput, streamPortIndex, mappings);
		auto const errorCallback = LocalEntityImpl<>::makeAemAECPErrorHandler(handler, &_controllerInterface, targetEntityID, std::placeholders::_1, streamPortIndex, s_emptyMappings);

		sendAemAecpCommand(targetEntityID, protocol::AemCommandType::RemoveAudioMappings, ser.data(), ser.size(), errorCallback, handler);
	}
	catch ([[maybe_unused]] std::exception const& e)
	{
		LOG_CONTROLLER_ENTITY_DEBUG(targetEntityID, "Failed to serialize removeStreamOutputAudioMappings: {}", e.what());
	}
}

void CapabilityDelegate::setStreamInputInfo(UniqueIdentifier const targetEntityID, model::StreamIndex const streamIndex, model::StreamInfo const& info, Interface::SetStreamInputInfoHandler const& handler) const noexcept
{
	try
	{
		auto const ser = protocol::aemPayload::serializeSetStreamInfoCommand(model::DescriptorType::StreamInput, streamIndex, info);
		auto const errorCallback = LocalEntityImpl<>::makeAemAECPErrorHandler(handler, &_controllerInterface, targetEntityID, std::placeholders::_1, streamIndex, s_emptyStreamInfo);

		sendAemAecpCommand(targetEntityID, protocol::AemCommandType::SetStreamInfo, ser.data(), ser.size(), errorCallback, handler);
	}
	catch ([[maybe_unused]] std::exception const& e)
	{
		LOG_CONTROLLER_ENTITY_DEBUG(targetEntityID, "Failed to serialize setStreamInputInfo: {}", e.what());
	}
}

void CapabilityDelegate::setStreamOutputInfo(UniqueIdentifier const targetEntityID, model::StreamIndex const streamIndex, model::StreamInfo const& info, Interface::SetStreamOutputInfoHandler const& handler) const noexcept
{
	try
	{
		auto const ser = protocol::aemPayload::serializeSetStreamInfoCommand(model::DescriptorType::StreamOutput, streamIndex, info);
		auto const errorCallback = LocalEntityImpl<>::makeAemAECPErrorHandler(handler, &_controllerInterface, targetEntityID, std::placeholders::_1, streamIndex, s_emptyStreamInfo);

		sendAemAecpCommand(targetEntityID, protocol::AemCommandType::SetStreamInfo, ser.data(), ser.size(), errorCallback, handler);
	}
	catch ([[maybe_unused]] std::exception const& e)
	{
		LOG_CONTROLLER_ENTITY_DEBUG(targetEntityID, "Failed to serialize setStreamOutputInfo: {}", e.what());
	}
}

void CapabilityDelegate::getStreamInputInfo(UniqueIdentifier const targetEntityID, model::StreamIndex const streamIndex, Interface::GetStreamInputInfoHandler const& handler) const noexcept
{
	try
	{
		auto const ser = protocol::aemPayload::serializeGetStreamInfoCommand(model::DescriptorType::StreamInput, streamIndex);
		auto const errorCallback = LocalEntityImpl<>::makeAemAECPErrorHandler(handler, &_controllerInterface, targetEntityID, std::placeholders::_1, streamIndex, s_emptyStreamInfo);

		sendAemAecpCommand(targetEntityID, protocol::AemCommandType::GetStreamInfo, ser.data(), ser.size(), errorCallback, handler);
	}
	catch ([[maybe_unused]] std::exception const& e)
	{
		LOG_CONTROLLER_ENTITY_DEBUG(targetEntityID, "Failed to serialize getStreamInputInfo: {}", e.what());
	}
}

void CapabilityDelegate::getStreamOutputInfo(UniqueIdentifier const targetEntityID, model::StreamIndex const streamIndex, Interface::GetStreamOutputInfoHandler const& handler) const noexcept
{
	try
	{
		auto const ser = protocol::aemPayload::serializeGetStreamInfoCommand(model::DescriptorType::StreamOutput, streamIndex);
		auto const errorCallback = LocalEntityImpl<>::makeAemAECPErrorHandler(handler, &_controllerInterface, targetEntityID, std::placeholders::_1, streamIndex, s_emptyStreamInfo);

		sendAemAecpCommand(targetEntityID, protocol::AemCommandType::GetStreamInfo, ser.data(), ser.size(), errorCallback, handler);
	}
	catch ([[maybe_unused]] std::exception const& e)
	{
		LOG_CONTROLLER_ENTITY_DEBUG(targetEntityID, "Failed to serialize getStreamOutputInfo: {}", e.what());
	}
}

void CapabilityDelegate::setEntityName(UniqueIdentifier const targetEntityID, model::AvdeccFixedString const& entityName, Interface::SetEntityNameHandler const& handler) const noexcept
{
	try
	{
		auto const ser = protocol::aemPayload::serializeSetNameCommand(model::DescriptorType::Entity, 0, 0, 0, entityName);
		auto const errorCallback = LocalEntityImpl<>::makeAemAECPErrorHandler(handler, &_controllerInterface, targetEntityID, std::placeholders::_1);

		sendAemAecpCommand(targetEntityID, protocol::AemCommandType::SetName, ser.data(), ser.size(), errorCallback, handler);
	}
	catch ([[maybe_unused]] std::exception const& e)
	{
		LOG_CONTROLLER_ENTITY_DEBUG(targetEntityID, "Failed to serialize setName: {}", e.what());
	}
}

void CapabilityDelegate::getEntityName(UniqueIdentifier const targetEntityID, Interface::GetEntityNameHandler const& handler) const noexcept
{
	try
	{
		auto const ser = protocol::aemPayload::serializeGetNameCommand(model::DescriptorType::Entity, 0, 0, 0);
		auto const errorCallback = LocalEntityImpl<>::makeAemAECPErrorHandler(handler, &_controllerInterface, targetEntityID, std::placeholders::_1, s_emptyAvdeccFixedString);

		sendAemAecpCommand(targetEntityID, protocol::AemCommandType::GetName, ser.data(), ser.size(), errorCallback, handler);
	}
	catch ([[maybe_unused]] std::exception const& e)
	{
		LOG_CONTROLLER_ENTITY_DEBUG(targetEntityID, "Failed to serialize getName: {}", e.what());
	}
}

void CapabilityDelegate::setEntityGroupName(UniqueIdentifier const targetEntityID, model::AvdeccFixedString const& entityGroupName, Interface::SetEntityGroupNameHandler const& handler) const noexcept
{
	try
	{
		auto const ser = protocol::aemPayload::serializeSetNameCommand(model::DescriptorType::Entity, 0, 1, 0, entityGroupName);
		auto const errorCallback = LocalEntityImpl<>::makeAemAECPErrorHandler(handler, &_controllerInterface, targetEntityID, std::placeholders::_1);

		sendAemAecpCommand(targetEntityID, protocol::AemCommandType::SetName, ser.data(), ser.size(), errorCallback, handler);
	}
	catch ([[maybe_unused]] std::exception const& e)
	{
		LOG_CONTROLLER_ENTITY_DEBUG(targetEntityID, "Failed to serialize setName: {}", e.what());
	}
}

void CapabilityDelegate::getEntityGroupName(UniqueIdentifier const targetEntityID, Interface::GetEntityGroupNameHandler const& handler) const noexcept
{
	try
	{
		auto const ser = protocol::aemPayload::serializeGetNameCommand(model::DescriptorType::Entity, 0, 1, 0);
		auto const errorCallback = LocalEntityImpl<>::makeAemAECPErrorHandler(handler, &_controllerInterface, targetEntityID, std::placeholders::_1, s_emptyAvdeccFixedString);

		sendAemAecpCommand(targetEntityID, protocol::AemCommandType::GetName, ser.data(), ser.size(), errorCallback, handler);
	}
	catch ([[maybe_unused]] std::exception const& e)
	{
		LOG_CONTROLLER_ENTITY_DEBUG(targetEntityID, "Failed to serialize getName: {}", e.what());
	}
}

void CapabilityDelegate::setConfigurationName(UniqueIdentifier const targetEntityID, model::ConfigurationIndex const configurationIndex, model::AvdeccFixedString const& entityGroupName, Interface::SetConfigurationNameHandler const& handler) const noexcept
{
	try
	{
		auto const ser = protocol::aemPayload::serializeSetNameCommand(model::DescriptorType::Configuration, configurationIndex, 0, 0, entityGroupName);
		auto const errorCallback = LocalEntityImpl<>::makeAemAECPErrorHandler(handler, &_controllerInterface, targetEntityID, std::placeholders::_1, configurationIndex);

		sendAemAecpCommand(targetEntityID, protocol::AemCommandType::SetName, ser.data(), ser.size(), errorCallback, handler);
	}
	catch ([[maybe_unused]] std::exception const& e)
	{
		LOG_CONTROLLER_ENTITY_DEBUG(targetEntityID, "Failed to serialize setName: {}", e.what());
	}
}

void CapabilityDelegate::getConfigurationName(UniqueIdentifier const targetEntityID, model::ConfigurationIndex const configurationIndex, Interface::GetConfigurationNameHandler const& handler) const noexcept
{
	try
	{
		auto const ser = protocol::aemPayload::serializeGetNameCommand(model::DescriptorType::Configuration, configurationIndex, 0, 0);
		auto const errorCallback = LocalEntityImpl<>::makeAemAECPErrorHandler(handler, &_controllerInterface, targetEntityID, std::placeholders::_1, configurationIndex, s_emptyAvdeccFixedString);

		sendAemAecpCommand(targetEntityID, protocol::AemCommandType::GetName, ser.data(), ser.size(), errorCallback, handler);
	}
	catch ([[maybe_unused]] std::exception const& e)
	{
		LOG_CONTROLLER_ENTITY_DEBUG(targetEntityID, "Failed to serialize getName: {}", e.what());
	}
}

void CapabilityDelegate::setAudioUnitName(UniqueIdentifier const targetEntityID, model::ConfigurationIndex const configurationIndex, model::AudioUnitIndex const audioUnitIndex, model::AvdeccFixedString const& audioUnitName, Interface::SetAudioUnitNameHandler const& handler) const noexcept
{
	try
	{
		auto const ser = protocol::aemPayload::serializeSetNameCommand(model::DescriptorType::AudioUnit, audioUnitIndex, 0, configurationIndex, audioUnitName);
		auto const errorCallback = LocalEntityImpl<>::makeAemAECPErrorHandler(handler, &_controllerInterface, targetEntityID, std::placeholders::_1, configurationIndex, audioUnitIndex);

		sendAemAecpCommand(targetEntityID, protocol::AemCommandType::SetName, ser.data(), ser.size(), errorCallback, handler);
	}
	catch ([[maybe_unused]] std::exception const& e)
	{
		LOG_CONTROLLER_ENTITY_DEBUG(targetEntityID, "Failed to serialize setName: {}", e.what());
	}
}

void CapabilityDelegate::getAudioUnitName(UniqueIdentifier const targetEntityID, model::ConfigurationIndex const configurationIndex, model::StreamIndex const audioUnitIndex, Interface::GetAudioUnitNameHandler const& handler) const noexcept
{
	try
	{
		auto const ser = protocol::aemPayload::serializeGetNameCommand(model::DescriptorType::AudioUnit, audioUnitIndex, 0, configurationIndex);
		auto const errorCallback = LocalEntityImpl<>::makeAemAECPErrorHandler(handler, &_controllerInterface, targetEntityID, std::placeholders::_1, configurationIndex, audioUnitIndex, s_emptyAvdeccFixedString);

		sendAemAecpCommand(targetEntityID, protocol::AemCommandType::GetName, ser.data(), ser.size(), errorCallback, handler);
	}
	catch ([[maybe_unused]] std::exception const& e)
	{
		LOG_CONTROLLER_ENTITY_DEBUG(targetEntityID, "Failed to serialize getName: {}", e.what());
	}
}

void CapabilityDelegate::setStreamInputName(UniqueIdentifier const targetEntityID, model::ConfigurationIndex const configurationIndex, model::StreamIndex const streamIndex, model::AvdeccFixedString const& streamInputName, Interface::SetStreamInputNameHandler const& handler) const noexcept
{
	try
	{
		auto const ser = protocol::aemPayload::serializeSetNameCommand(model::DescriptorType::StreamInput, streamIndex, 0, configurationIndex, streamInputName);
		auto const errorCallback = LocalEntityImpl<>::makeAemAECPErrorHandler(handler, &_controllerInterface, targetEntityID, std::placeholders::_1, configurationIndex, streamIndex);

		sendAemAecpCommand(targetEntityID, protocol::AemCommandType::SetName, ser.data(), ser.size(), errorCallback, handler);
	}
	catch ([[maybe_unused]] std::exception const& e)
	{
		LOG_CONTROLLER_ENTITY_DEBUG(targetEntityID, "Failed to serialize setName: {}", e.what());
	}
}

void CapabilityDelegate::getStreamInputName(UniqueIdentifier const targetEntityID, model::ConfigurationIndex const configurationIndex, model::StreamIndex const streamIndex, Interface::GetStreamInputNameHandler const& handler) const noexcept
{
	try
	{
		auto const ser = protocol::aemPayload::serializeGetNameCommand(model::DescriptorType::StreamInput, streamIndex, 0, configurationIndex);
		auto const errorCallback = LocalEntityImpl<>::makeAemAECPErrorHandler(handler, &_controllerInterface, targetEntityID, std::placeholders::_1, configurationIndex, streamIndex, s_emptyAvdeccFixedString);

		sendAemAecpCommand(targetEntityID, protocol::AemCommandType::GetName, ser.data(), ser.size(), errorCallback, handler);
	}
	catch ([[maybe_unused]] std::exception const& e)
	{
		LOG_CONTROLLER_ENTITY_DEBUG(targetEntityID, "Failed to serialize getName: {}", e.what());
	}
}

void CapabilityDelegate::setStreamOutputName(UniqueIdentifier const targetEntityID, model::ConfigurationIndex const configurationIndex, model::StreamIndex const streamIndex, model::AvdeccFixedString const& streamOutputName, Interface::SetStreamOutputNameHandler const& handler) const noexcept
{
	try
	{
		auto const ser = protocol::aemPayload::serializeSetNameCommand(model::DescriptorType::StreamOutput, streamIndex, 0, configurationIndex, streamOutputName);
		auto const errorCallback = LocalEntityImpl<>::makeAemAECPErrorHandler(handler, &_controllerInterface, targetEntityID, std::placeholders::_1, configurationIndex, streamIndex);

		sendAemAecpCommand(targetEntityID, protocol::AemCommandType::SetName, ser.data(), ser.size(), errorCallback, handler);
	}
	catch ([[maybe_unused]] std::exception const& e)
	{
		LOG_CONTROLLER_ENTITY_DEBUG(targetEntityID, "Failed to serialize setName: {}", e.what());
	}
}

void CapabilityDelegate::getStreamOutputName(UniqueIdentifier const targetEntityID, model::ConfigurationIndex const configurationIndex, model::StreamIndex const streamIndex, Interface::GetStreamOutputNameHandler const& handler) const noexcept
{
	try
	{
		auto const ser = protocol::aemPayload::serializeGetNameCommand(model::DescriptorType::StreamOutput, streamIndex, 0, configurationIndex);
		auto const errorCallback = LocalEntityImpl<>::makeAemAECPErrorHandler(handler, &_controllerInterface, targetEntityID, std::placeholders::_1, configurationIndex, streamIndex, s_emptyAvdeccFixedString);

		sendAemAecpCommand(targetEntityID, protocol::AemCommandType::GetName, ser.data(), ser.size(), errorCallback, handler);
	}
	catch ([[maybe_unused]] std::exception const& e)
	{
		LOG_CONTROLLER_ENTITY_DEBUG(targetEntityID, "Failed to serialize getName: {}", e.what());
	}
}

void CapabilityDelegate::setAvbInterfaceName(UniqueIdentifier const targetEntityID, model::ConfigurationIndex const configurationIndex, model::AvbInterfaceIndex const avbInterfaceIndex, model::AvdeccFixedString const& avbInterfaceName, Interface::SetAvbInterfaceNameHandler const& handler) const noexcept
{
	try
	{
		auto const ser = protocol::aemPayload::serializeSetNameCommand(model::DescriptorType::AvbInterface, avbInterfaceIndex, 0, configurationIndex, avbInterfaceName);
		auto const errorCallback = LocalEntityImpl<>::makeAemAECPErrorHandler(handler, &_controllerInterface, targetEntityID, std::placeholders::_1, configurationIndex, avbInterfaceIndex);

		sendAemAecpCommand(targetEntityID, protocol::AemCommandType::SetName, ser.data(), ser.size(), errorCallback, handler);
	}
	catch ([[maybe_unused]] std::exception const& e)
	{
		LOG_CONTROLLER_ENTITY_DEBUG(targetEntityID, "Failed to serialize setName: {}", e.what());
	}
}

void CapabilityDelegate::getAvbInterfaceName(UniqueIdentifier const targetEntityID, model::ConfigurationIndex const configurationIndex, model::StreamIndex const avbInterfaceIndex, Interface::GetAvbInterfaceNameHandler const& handler) const noexcept
{
	try
	{
		auto const ser = protocol::aemPayload::serializeGetNameCommand(model::DescriptorType::AvbInterface, avbInterfaceIndex, 0, configurationIndex);
		auto const errorCallback = LocalEntityImpl<>::makeAemAECPErrorHandler(handler, &_controllerInterface, targetEntityID, std::placeholders::_1, configurationIndex, avbInterfaceIndex, s_emptyAvdeccFixedString);

		sendAemAecpCommand(targetEntityID, protocol::AemCommandType::GetName, ser.data(), ser.size(), errorCallback, handler);
	}
	catch ([[maybe_unused]] std::exception const& e)
	{
		LOG_CONTROLLER_ENTITY_DEBUG(targetEntityID, "Failed to serialize getName: {}", e.what());
	}
}

void CapabilityDelegate::setClockSourceName(UniqueIdentifier const targetEntityID, model::ConfigurationIndex const configurationIndex, model::ClockSourceIndex const clockSourceIndex, model::AvdeccFixedString const& clockSourceName, Interface::SetClockSourceNameHandler const& handler) const noexcept
{
	try
	{
		auto const ser = protocol::aemPayload::serializeSetNameCommand(model::DescriptorType::ClockSource, clockSourceIndex, 0, configurationIndex, clockSourceName);
		auto const errorCallback = LocalEntityImpl<>::makeAemAECPErrorHandler(handler, &_controllerInterface, targetEntityID, std::placeholders::_1, configurationIndex, clockSourceIndex);

		sendAemAecpCommand(targetEntityID, protocol::AemCommandType::SetName, ser.data(), ser.size(), errorCallback, handler);
	}
	catch ([[maybe_unused]] std::exception const& e)
	{
		LOG_CONTROLLER_ENTITY_DEBUG(targetEntityID, "Failed to serialize setName: {}", e.what());
	}
}

void CapabilityDelegate::getClockSourceName(UniqueIdentifier const targetEntityID, model::ConfigurationIndex const configurationIndex, model::StreamIndex const clockSourceIndex, Interface::GetClockSourceNameHandler const& handler) const noexcept
{
	try
	{
		auto const ser = protocol::aemPayload::serializeGetNameCommand(model::DescriptorType::ClockSource, clockSourceIndex, 0, configurationIndex);
		auto const errorCallback = LocalEntityImpl<>::makeAemAECPErrorHandler(handler, &_controllerInterface, targetEntityID, std::placeholders::_1, configurationIndex, clockSourceIndex, s_emptyAvdeccFixedString);

		sendAemAecpCommand(targetEntityID, protocol::AemCommandType::GetName, ser.data(), ser.size(), errorCallback, handler);
	}
	catch ([[maybe_unused]] std::exception const& e)
	{
		LOG_CONTROLLER_ENTITY_DEBUG(targetEntityID, "Failed to serialize getName: {}", e.what());
	}
}

void CapabilityDelegate::setMemoryObjectName(UniqueIdentifier const targetEntityID, model::ConfigurationIndex const configurationIndex, model::MemoryObjectIndex const memoryObjectIndex, model::AvdeccFixedString const& memoryObjectName, Interface::SetMemoryObjectNameHandler const& handler) const noexcept
{
	try
	{
		auto const ser = protocol::aemPayload::serializeSetNameCommand(model::DescriptorType::MemoryObject, memoryObjectIndex, 0, configurationIndex, memoryObjectName);
		auto const errorCallback = LocalEntityImpl<>::makeAemAECPErrorHandler(handler, &_controllerInterface, targetEntityID, std::placeholders::_1, configurationIndex, memoryObjectIndex);

		sendAemAecpCommand(targetEntityID, protocol::AemCommandType::SetName, ser.data(), ser.size(), errorCallback, handler);
	}
	catch ([[maybe_unused]] std::exception const& e)
	{
		LOG_CONTROLLER_ENTITY_DEBUG(targetEntityID, "Failed to serialize setName: {}", e.what());
	}
}

void CapabilityDelegate::getMemoryObjectName(UniqueIdentifier const targetEntityID, model::ConfigurationIndex const configurationIndex, model::StreamIndex const memoryObjectIndex, Interface::GetMemoryObjectNameHandler const& handler) const noexcept
{
	try
	{
		auto const ser = protocol::aemPayload::serializeGetNameCommand(model::DescriptorType::MemoryObject, memoryObjectIndex, 0, configurationIndex);
		auto const errorCallback = LocalEntityImpl<>::makeAemAECPErrorHandler(handler, &_controllerInterface, targetEntityID, std::placeholders::_1, configurationIndex, memoryObjectIndex, s_emptyAvdeccFixedString);

		sendAemAecpCommand(targetEntityID, protocol::AemCommandType::GetName, ser.data(), ser.size(), errorCallback, handler);
	}
	catch ([[maybe_unused]] std::exception const& e)
	{
		LOG_CONTROLLER_ENTITY_DEBUG(targetEntityID, "Failed to serialize getName: {}", e.what());
	}
}

void CapabilityDelegate::setAudioClusterName(UniqueIdentifier const targetEntityID, model::ConfigurationIndex const configurationIndex, model::ClusterIndex const audioClusterIndex, model::AvdeccFixedString const& audioClusterName, Interface::SetAudioClusterNameHandler const& handler) const noexcept
{
	try
	{
		auto const ser = protocol::aemPayload::serializeSetNameCommand(model::DescriptorType::AudioCluster, audioClusterIndex, 0, configurationIndex, audioClusterName);
		auto const errorCallback = LocalEntityImpl<>::makeAemAECPErrorHandler(handler, &_controllerInterface, targetEntityID, std::placeholders::_1, configurationIndex, audioClusterIndex);

		sendAemAecpCommand(targetEntityID, protocol::AemCommandType::SetName, ser.data(), ser.size(), errorCallback, handler);
	}
	catch ([[maybe_unused]] std::exception const& e)
	{
		LOG_CONTROLLER_ENTITY_DEBUG(targetEntityID, "Failed to serialize setName: {}", e.what());
	}
}

void CapabilityDelegate::getAudioClusterName(UniqueIdentifier const targetEntityID, model::ConfigurationIndex const configurationIndex, model::StreamIndex const audioClusterIndex, Interface::GetAudioClusterNameHandler const& handler) const noexcept
{
	try
	{
		auto const ser = protocol::aemPayload::serializeGetNameCommand(model::DescriptorType::AudioCluster, audioClusterIndex, 0, configurationIndex);
		auto const errorCallback = LocalEntityImpl<>::makeAemAECPErrorHandler(handler, &_controllerInterface, targetEntityID, std::placeholders::_1, configurationIndex, audioClusterIndex, s_emptyAvdeccFixedString);

		sendAemAecpCommand(targetEntityID, protocol::AemCommandType::GetName, ser.data(), ser.size(), errorCallback, handler);
	}
	catch ([[maybe_unused]] std::exception const& e)
	{
		LOG_CONTROLLER_ENTITY_DEBUG(targetEntityID, "Failed to serialize getName: {}", e.what());
	}
}

void CapabilityDelegate::setClockDomainName(UniqueIdentifier const targetEntityID, model::ConfigurationIndex const configurationIndex, model::ClockDomainIndex const clockDomainIndex, model::AvdeccFixedString const& clockDomainName, Interface::SetClockDomainNameHandler const& handler) const noexcept
{
	try
	{
		auto const ser = protocol::aemPayload::serializeSetNameCommand(model::DescriptorType::ClockDomain, clockDomainIndex, 0, configurationIndex, clockDomainName);
		auto const errorCallback = LocalEntityImpl<>::makeAemAECPErrorHandler(handler, &_controllerInterface, targetEntityID, std::placeholders::_1, configurationIndex, clockDomainIndex);

		sendAemAecpCommand(targetEntityID, protocol::AemCommandType::SetName, ser.data(), ser.size(), errorCallback, handler);
	}
	catch ([[maybe_unused]] std::exception const& e)
	{
		LOG_CONTROLLER_ENTITY_DEBUG(targetEntityID, "Failed to serialize setName: {}", e.what());
	}
}

void CapabilityDelegate::getClockDomainName(UniqueIdentifier const targetEntityID, model::ConfigurationIndex const configurationIndex, model::StreamIndex const clockDomainIndex, Interface::GetClockDomainNameHandler const& handler) const noexcept
{
	try
	{
		auto const ser = protocol::aemPayload::serializeGetNameCommand(model::DescriptorType::ClockDomain, clockDomainIndex, 0, configurationIndex);
		auto const errorCallback = LocalEntityImpl<>::makeAemAECPErrorHandler(handler, &_controllerInterface, targetEntityID, std::placeholders::_1, configurationIndex, clockDomainIndex, s_emptyAvdeccFixedString);

		sendAemAecpCommand(targetEntityID, protocol::AemCommandType::GetName, ser.data(), ser.size(), errorCallback, handler);
	}
	catch ([[maybe_unused]] std::exception const& e)
	{
		LOG_CONTROLLER_ENTITY_DEBUG(targetEntityID, "Failed to serialize getName: {}", e.what());
	}
}

void CapabilityDelegate::setAudioUnitSamplingRate(UniqueIdentifier const targetEntityID, model::AudioUnitIndex const audioUnitIndex, model::SamplingRate const samplingRate, Interface::SetAudioUnitSamplingRateHandler const& handler) const noexcept
{
	try
	{
		auto const ser = protocol::aemPayload::serializeSetSamplingRateCommand(model::DescriptorType::AudioUnit, audioUnitIndex, samplingRate);
		auto const errorCallback = LocalEntityImpl<>::makeAemAECPErrorHandler(handler, &_controllerInterface, targetEntityID, std::placeholders::_1, audioUnitIndex, model::getNullSamplingRate());

		sendAemAecpCommand(targetEntityID, protocol::AemCommandType::SetSamplingRate, ser.data(), ser.size(), errorCallback, handler);
	}
	catch ([[maybe_unused]] std::exception const& e)
	{
		LOG_CONTROLLER_ENTITY_DEBUG(targetEntityID, "Failed to serialize setAudioUnitSamplingRate: {}", e.what());
	}
}

void CapabilityDelegate::getAudioUnitSamplingRate(UniqueIdentifier const targetEntityID, model::AudioUnitIndex const audioUnitIndex, Interface::GetAudioUnitSamplingRateHandler const& handler) const noexcept
{
	try
	{
		auto const ser = protocol::aemPayload::serializeGetSamplingRateCommand(model::DescriptorType::AudioUnit, audioUnitIndex);
		auto const errorCallback = LocalEntityImpl<>::makeAemAECPErrorHandler(handler, &_controllerInterface, targetEntityID, std::placeholders::_1, audioUnitIndex, model::getNullSamplingRate());

		sendAemAecpCommand(targetEntityID, protocol::AemCommandType::GetSamplingRate, ser.data(), ser.size(), errorCallback, handler);
	}
	catch ([[maybe_unused]] std::exception const& e)
	{
		LOG_CONTROLLER_ENTITY_DEBUG(targetEntityID, "Failed to serialize getAudioUnitSamplingRate: {}", e.what());
	}
}

void CapabilityDelegate::setVideoClusterSamplingRate(UniqueIdentifier const targetEntityID, model::ClusterIndex const videoClusterIndex, model::SamplingRate const samplingRate, Interface::SetVideoClusterSamplingRateHandler const& handler) const noexcept
{
	try
	{
		auto const ser = protocol::aemPayload::serializeSetSamplingRateCommand(model::DescriptorType::VideoCluster, videoClusterIndex, samplingRate);
		auto const errorCallback = LocalEntityImpl<>::makeAemAECPErrorHandler(handler, &_controllerInterface, targetEntityID, std::placeholders::_1, videoClusterIndex, model::getNullSamplingRate());

		sendAemAecpCommand(targetEntityID, protocol::AemCommandType::SetSamplingRate, ser.data(), ser.size(), errorCallback, handler);
	}
	catch ([[maybe_unused]] std::exception const& e)
	{
		LOG_CONTROLLER_ENTITY_DEBUG(targetEntityID, "Failed to serialize setVideoClusterSamplingRate: {}", e.what());
	}
}

void CapabilityDelegate::getVideoClusterSamplingRate(UniqueIdentifier const targetEntityID, model::ClusterIndex const videoClusterIndex, Interface::GetVideoClusterSamplingRateHandler const& handler) const noexcept
{
	try
	{
		auto const ser = protocol::aemPayload::serializeGetSamplingRateCommand(model::DescriptorType::VideoCluster, videoClusterIndex);
		auto const errorCallback = LocalEntityImpl<>::makeAemAECPErrorHandler(handler, &_controllerInterface, targetEntityID, std::placeholders::_1, videoClusterIndex, model::getNullSamplingRate());

		sendAemAecpCommand(targetEntityID, protocol::AemCommandType::GetSamplingRate, ser.data(), ser.size(), errorCallback, handler);
	}
	catch ([[maybe_unused]] std::exception const& e)
	{
		LOG_CONTROLLER_ENTITY_DEBUG(targetEntityID, "Failed to serialize getVideoClusterSamplingRate: {}", e.what());
	}
}

void CapabilityDelegate::setSensorClusterSamplingRate(UniqueIdentifier const targetEntityID, model::ClusterIndex const sensorClusterIndex, model::SamplingRate const samplingRate, Interface::SetSensorClusterSamplingRateHandler const& handler) const noexcept
{
	try
	{
		auto const ser = protocol::aemPayload::serializeSetSamplingRateCommand(model::DescriptorType::SensorCluster, sensorClusterIndex, samplingRate);
		auto const errorCallback = LocalEntityImpl<>::makeAemAECPErrorHandler(handler, &_controllerInterface, targetEntityID, std::placeholders::_1, sensorClusterIndex, model::getNullSamplingRate());

		sendAemAecpCommand(targetEntityID, protocol::AemCommandType::SetSamplingRate, ser.data(), ser.size(), errorCallback, handler);
	}
	catch ([[maybe_unused]] std::exception const& e)
	{
		LOG_CONTROLLER_ENTITY_DEBUG(targetEntityID, "Failed to serialize setSensorClusterSamplingRate: {}", e.what());
	}
}

void CapabilityDelegate::getSensorClusterSamplingRate(UniqueIdentifier const targetEntityID, model::ClusterIndex const sensorClusterIndex, Interface::GetSensorClusterSamplingRateHandler const& handler) const noexcept
{
	try
	{
		auto const ser = protocol::aemPayload::serializeGetSamplingRateCommand(model::DescriptorType::SensorCluster, sensorClusterIndex);
		auto const errorCallback = LocalEntityImpl<>::makeAemAECPErrorHandler(handler, &_controllerInterface, targetEntityID, std::placeholders::_1, sensorClusterIndex, model::getNullSamplingRate());

		sendAemAecpCommand(targetEntityID, protocol::AemCommandType::GetSamplingRate, ser.data(), ser.size(), errorCallback, handler);
	}
	catch ([[maybe_unused]] std::exception const& e)
	{
		LOG_CONTROLLER_ENTITY_DEBUG(targetEntityID, "Failed to serialize getSensorClusterSamplingRate: {}", e.what());
	}
}

void CapabilityDelegate::setClockSource(UniqueIdentifier const targetEntityID, model::ClockDomainIndex const clockDomainIndex, model::ClockSourceIndex const clockSourceIndex, Interface::SetClockSourceHandler const& handler) const noexcept
{
	try
	{
		auto const ser = protocol::aemPayload::serializeSetClockSourceCommand(model::DescriptorType::ClockDomain, clockDomainIndex, clockSourceIndex);
		auto const errorCallback = LocalEntityImpl<>::makeAemAECPErrorHandler(handler, &_controllerInterface, targetEntityID, std::placeholders::_1, clockDomainIndex, model::ClockSourceIndex{ 0u });

		sendAemAecpCommand(targetEntityID, protocol::AemCommandType::SetClockSource, ser.data(), ser.size(), errorCallback, handler);
	}
	catch ([[maybe_unused]] std::exception const& e)
	{
		LOG_CONTROLLER_ENTITY_DEBUG(targetEntityID, "Failed to serialize setClockSource: {}", e.what());
	}
}

void CapabilityDelegate::getClockSource(UniqueIdentifier const targetEntityID, model::ClockDomainIndex const clockDomainIndex, Interface::GetClockSourceHandler const& handler) const noexcept
{
	try
	{
		auto const ser = protocol::aemPayload::serializeGetClockSourceCommand(model::DescriptorType::ClockDomain, clockDomainIndex);
		auto const errorCallback = LocalEntityImpl<>::makeAemAECPErrorHandler(handler, &_controllerInterface, targetEntityID, std::placeholders::_1, clockDomainIndex, model::ClockSourceIndex{ 0u });

		sendAemAecpCommand(targetEntityID, protocol::AemCommandType::GetClockSource, ser.data(), ser.size(), errorCallback, handler);
	}
	catch ([[maybe_unused]] std::exception const& e)
	{
		LOG_CONTROLLER_ENTITY_DEBUG(targetEntityID, "Failed to serialize getClockSource: {}", e.what());
	}
}

void CapabilityDelegate::startStreamInput(UniqueIdentifier const targetEntityID, model::StreamIndex const streamIndex, Interface::StartStreamInputHandler const& handler) const noexcept
{
	try
	{
		auto const ser = protocol::aemPayload::serializeStartStreamingCommand(model::DescriptorType::StreamInput, streamIndex);
		auto const errorCallback = LocalEntityImpl<>::makeAemAECPErrorHandler(handler, &_controllerInterface, targetEntityID, std::placeholders::_1, streamIndex);

		sendAemAecpCommand(targetEntityID, protocol::AemCommandType::StartStreaming, ser.data(), ser.size(), errorCallback, handler);
	}
	catch ([[maybe_unused]] std::exception const& e)
	{
		LOG_CONTROLLER_ENTITY_DEBUG(targetEntityID, "Failed to serialize startStreamInput: {}", e.what());
	}
}

void CapabilityDelegate::startStreamOutput(UniqueIdentifier const targetEntityID, model::StreamIndex const streamIndex, Interface::StartStreamOutputHandler const& handler) const noexcept
{
	try
	{
		auto const ser = protocol::aemPayload::serializeStartStreamingCommand(model::DescriptorType::StreamOutput, streamIndex);
		auto const errorCallback = LocalEntityImpl<>::makeAemAECPErrorHandler(handler, &_controllerInterface, targetEntityID, std::placeholders::_1, streamIndex);

		sendAemAecpCommand(targetEntityID, protocol::AemCommandType::StartStreaming, ser.data(), ser.size(), errorCallback, handler);
	}
	catch ([[maybe_unused]] std::exception const& e)
	{
		LOG_CONTROLLER_ENTITY_DEBUG(targetEntityID, "Failed to serialize startStreamOutput: {}", e.what());
	}
}

void CapabilityDelegate::stopStreamInput(UniqueIdentifier const targetEntityID, model::StreamIndex const streamIndex, Interface::StopStreamInputHandler const& handler) const noexcept
{
	try
	{
		auto const ser = protocol::aemPayload::serializeStopStreamingCommand(model::DescriptorType::StreamInput, streamIndex);
		auto const errorCallback = LocalEntityImpl<>::makeAemAECPErrorHandler(handler, &_controllerInterface, targetEntityID, std::placeholders::_1, streamIndex);

		sendAemAecpCommand(targetEntityID, protocol::AemCommandType::StopStreaming, ser.data(), ser.size(), errorCallback, handler);
	}
	catch ([[maybe_unused]] std::exception const& e)
	{
		LOG_CONTROLLER_ENTITY_DEBUG(targetEntityID, "Failed to serialize stopStreamInput: {}", e.what());
	}
}

void CapabilityDelegate::stopStreamOutput(UniqueIdentifier const targetEntityID, model::StreamIndex const streamIndex, Interface::StopStreamOutputHandler const& handler) const noexcept
{
	try
	{
		auto const ser = protocol::aemPayload::serializeStopStreamingCommand(model::DescriptorType::StreamOutput, streamIndex);
		auto const errorCallback = LocalEntityImpl<>::makeAemAECPErrorHandler(handler, &_controllerInterface, targetEntityID, std::placeholders::_1, streamIndex);

		sendAemAecpCommand(targetEntityID, protocol::AemCommandType::StopStreaming, ser.data(), ser.size(), errorCallback, handler);
	}
	catch ([[maybe_unused]] std::exception const& e)
	{
		LOG_CONTROLLER_ENTITY_DEBUG(targetEntityID, "Failed to serialize stopStreamOutput: {}", e.what());
	}
}

void CapabilityDelegate::getAvbInfo(UniqueIdentifier const targetEntityID, model::AvbInterfaceIndex const avbInterfaceIndex, Interface::GetAvbInfoHandler const& handler) const noexcept
{
	try
	{
		auto const ser = protocol::aemPayload::serializeGetAvbInfoCommand(model::DescriptorType::AvbInterface, avbInterfaceIndex);
		auto const errorCallback = LocalEntityImpl<>::makeAemAECPErrorHandler(handler, &_controllerInterface, targetEntityID, std::placeholders::_1, avbInterfaceIndex, s_emptyAvbInfo);

		sendAemAecpCommand(targetEntityID, protocol::AemCommandType::GetAvbInfo, ser.data(), ser.size(), errorCallback, handler);
	}
	catch ([[maybe_unused]] std::exception const& e)
	{
		LOG_CONTROLLER_ENTITY_DEBUG(targetEntityID, "Failed to serialize getAvbInfo: {}", e.what());
	}
}

void CapabilityDelegate::getAsPath(UniqueIdentifier const targetEntityID, model::AvbInterfaceIndex const avbInterfaceIndex, Interface::GetAsPathHandler const& handler) const noexcept
{
	try
	{
		auto const ser = protocol::aemPayload::serializeGetAsPathCommand(avbInterfaceIndex);
		auto const errorCallback = LocalEntityImpl<>::makeAemAECPErrorHandler(handler, &_controllerInterface, targetEntityID, std::placeholders::_1, avbInterfaceIndex, s_emptyAsPath);

		sendAemAecpCommand(targetEntityID, protocol::AemCommandType::GetAsPath, ser.data(), ser.size(), errorCallback, handler);
	}
	catch ([[maybe_unused]] std::exception const& e)
	{
		LOG_CONTROLLER_ENTITY_DEBUG(targetEntityID, "Failed to serialize getAsPath: {}", e.what());
	}
}

void CapabilityDelegate::getAvbInterfaceCounters(UniqueIdentifier const targetEntityID, model::AvbInterfaceIndex const avbInterfaceIndex, Interface::GetAvbInterfaceCountersHandler const& handler) const noexcept
{
	try
	{
		auto const ser = protocol::aemPayload::serializeGetCountersCommand(model::DescriptorType::AvbInterface, avbInterfaceIndex);
		auto const errorCallback = LocalEntityImpl<>::makeAemAECPErrorHandler(handler, &_controllerInterface, targetEntityID, std::placeholders::_1, avbInterfaceIndex, AvbInterfaceCounterValidFlags{}, model::DescriptorCounters{});

		sendAemAecpCommand(targetEntityID, protocol::AemCommandType::GetCounters, ser.data(), ser.size(), errorCallback, handler);
	}
	catch ([[maybe_unused]] std::exception const& e)
	{
		LOG_CONTROLLER_ENTITY_DEBUG(targetEntityID, "Failed to serialize getAvbInterfaceCounters: {}", e.what());
	}
}

void CapabilityDelegate::getClockDomainCounters(UniqueIdentifier const targetEntityID, model::ClockDomainIndex const clockDomainIndex, Interface::GetClockDomainCountersHandler const& handler) const noexcept
{
	try
	{
		auto const ser = protocol::aemPayload::serializeGetCountersCommand(model::DescriptorType::ClockDomain, clockDomainIndex);
		auto const errorCallback = LocalEntityImpl<>::makeAemAECPErrorHandler(handler, &_controllerInterface, targetEntityID, std::placeholders::_1, clockDomainIndex, ClockDomainCounterValidFlags{}, model::DescriptorCounters{});

		sendAemAecpCommand(targetEntityID, protocol::AemCommandType::GetCounters, ser.data(), ser.size(), errorCallback, handler);
	}
	catch ([[maybe_unused]] std::exception const& e)
	{
		LOG_CONTROLLER_ENTITY_DEBUG(targetEntityID, "Failed to serialize getClockDomainCounters: {}", e.what());
	}
}

void CapabilityDelegate::getStreamInputCounters(UniqueIdentifier const targetEntityID, model::StreamIndex const streamIndex, Interface::GetStreamInputCountersHandler const& handler) const noexcept
{
	try
	{
		auto const ser = protocol::aemPayload::serializeGetCountersCommand(model::DescriptorType::StreamInput, streamIndex);
		auto const errorCallback = LocalEntityImpl<>::makeAemAECPErrorHandler(handler, &_controllerInterface, targetEntityID, std::placeholders::_1, streamIndex, StreamInputCounterValidFlags{}, model::DescriptorCounters{});

		sendAemAecpCommand(targetEntityID, protocol::AemCommandType::GetCounters, ser.data(), ser.size(), errorCallback, handler);
	}
	catch ([[maybe_unused]] std::exception const& e)
	{
		LOG_CONTROLLER_ENTITY_DEBUG(targetEntityID, "Failed to serialize getStreamInputCounters: {}", e.what());
	}
}

void CapabilityDelegate::getStreamOutputCounters(UniqueIdentifier const targetEntityID, model::StreamIndex const streamIndex, Interface::GetStreamOutputCountersHandler const& handler) const noexcept
{
	try
	{
		auto const ser = protocol::aemPayload::serializeGetCountersCommand(model::DescriptorType::StreamOutput, streamIndex);
		auto const errorCallback = LocalEntityImpl<>::makeAemAECPErrorHandler(handler, &_controllerInterface, targetEntityID, std::placeholders::_1, streamIndex, StreamOutputCounterValidFlags{}, model::DescriptorCounters{});

		sendAemAecpCommand(targetEntityID, protocol::AemCommandType::GetCounters, ser.data(), ser.size(), errorCallback, handler);
	}
	catch ([[maybe_unused]] std::exception const& e)
	{
		LOG_CONTROLLER_ENTITY_DEBUG(targetEntityID, "Failed to serialize getStreamOutputCounters: {}", e.what());
	}
}

void CapabilityDelegate::startOperation(UniqueIdentifier const targetEntityID, model::DescriptorType const descriptorType, model::DescriptorIndex const descriptorIndex, model::MemoryObjectOperationType const operationType, MemoryBuffer const& memoryBuffer, Interface::StartOperationHandler const& handler) const noexcept
{
	try
	{
		auto const ser = protocol::aemPayload::serializeStartOperationCommand(descriptorType, descriptorIndex, model::OperationID{ 0u }, operationType, memoryBuffer);
		auto const errorCallback = LocalEntityImpl<>::makeAemAECPErrorHandler(handler, &_controllerInterface, targetEntityID, std::placeholders::_1, descriptorType, descriptorIndex, model::OperationID{ 0u }, operationType, MemoryBuffer{});

		sendAemAecpCommand(targetEntityID, protocol::AemCommandType::StartOperation, ser.data(), ser.size(), errorCallback, handler);
	}
	catch ([[maybe_unused]] std::exception const& e)
	{
		LOG_CONTROLLER_ENTITY_DEBUG(targetEntityID, "Failed to serialize startOperation: {}", e.what());
	}
}

void CapabilityDelegate::abortOperation(UniqueIdentifier const targetEntityID, model::DescriptorType const descriptorType, model::DescriptorIndex const descriptorIndex, model::OperationID const operationID, Interface::AbortOperationHandler const& handler) const noexcept
{
	try
	{
		auto const ser = protocol::aemPayload::serializeAbortOperationCommand(descriptorType, descriptorIndex, operationID);
		auto const errorCallback = LocalEntityImpl<>::makeAemAECPErrorHandler(handler, &_controllerInterface, targetEntityID, std::placeholders::_1, descriptorType, descriptorIndex, operationID);

		sendAemAecpCommand(targetEntityID, protocol::AemCommandType::AbortOperation, ser.data(), ser.size(), errorCallback, handler);
	}
	catch ([[maybe_unused]] std::exception const& e)
	{
		LOG_CONTROLLER_ENTITY_DEBUG(targetEntityID, "Failed to serialize abortOperation: {}", e.what());
	}
}

void CapabilityDelegate::setMemoryObjectLength(UniqueIdentifier const targetEntityID, model::ConfigurationIndex const configurationIndex, model::MemoryObjectIndex const memoryObjectIndex, std::uint64_t const length, Interface::SetMemoryObjectLengthHandler const& handler) const noexcept
{
	try
	{
		auto const ser = protocol::aemPayload::serializeSetMemoryObjectLengthCommand(configurationIndex, memoryObjectIndex, length);
		auto const errorCallback = LocalEntityImpl<>::makeAemAECPErrorHandler(handler, &_controllerInterface, targetEntityID, std::placeholders::_1, configurationIndex, memoryObjectIndex, std::uint64_t{ 0u });

		sendAemAecpCommand(targetEntityID, protocol::AemCommandType::SetMemoryObjectLength, ser.data(), ser.size(), errorCallback, handler);
	}
	catch ([[maybe_unused]] std::exception const& e)
	{
		LOG_CONTROLLER_ENTITY_DEBUG(targetEntityID, "Failed to serialize setMemoryObjectLength: {}", e.what());
	}
}

void CapabilityDelegate::getMemoryObjectLength(UniqueIdentifier const targetEntityID, model::ConfigurationIndex const configurationIndex, model::MemoryObjectIndex const memoryObjectIndex, Interface::GetMemoryObjectLengthHandler const& handler) const noexcept
{
	try
	{
		auto const ser = protocol::aemPayload::serializeGetMemoryObjectLengthCommand(configurationIndex, memoryObjectIndex);
		auto const errorCallback = LocalEntityImpl<>::makeAemAECPErrorHandler(handler, &_controllerInterface, targetEntityID, std::placeholders::_1, configurationIndex, memoryObjectIndex, std::uint64_t{ 0u });

		sendAemAecpCommand(targetEntityID, protocol::AemCommandType::GetMemoryObjectLength, ser.data(), ser.size(), errorCallback, handler);
	}
	catch ([[maybe_unused]] std::exception const& e)
	{
		LOG_CONTROLLER_ENTITY_DEBUG(targetEntityID, "Failed to serialize getMemoryObjectLength: {}", e.what());
	}
}

/* Enumeration and Control Protocol (AECP) AA */
void CapabilityDelegate::addressAccess(la::avdecc::UniqueIdentifier const targetEntityID, addressAccess::Tlvs const& tlvs, Interface::AddressAccessHandler const& handler) const noexcept
{
	try
	{
		auto const errorCallback = LocalEntityImpl<>::makeAaAECPErrorHandler(handler, &_controllerInterface, targetEntityID, std::placeholders::_1, addressAccess::Tlvs{});

		sendAaAecpCommand(targetEntityID, tlvs, errorCallback, handler);
	}
	catch ([[maybe_unused]] std::exception const& e)
	{
		LOG_CONTROLLER_ENTITY_DEBUG(targetEntityID, "Failed to serialize addressAccess: {}", e.what());
	}
}

/* Enumeration and Control Protocol (AECP) MVU (Milan Vendor Unique) */
void CapabilityDelegate::getMilanInfo(UniqueIdentifier const targetEntityID, Interface::GetMilanInfoHandler const& handler) const noexcept
{
	try
	{
		auto const ser = protocol::mvuPayload::serializeGetMilanInfoCommand();
		auto const errorCallback = LocalEntityImpl<>::makeMvuAECPErrorHandler(handler, &_controllerInterface, targetEntityID, std::placeholders::_1, s_emptyMilanInfo);

		sendMvuAecpCommand(targetEntityID, protocol::MvuCommandType::GetMilanInfo, ser.data(), ser.size(), errorCallback, handler);
	}
	catch ([[maybe_unused]] std::exception const& e)
	{
		LOG_CONTROLLER_ENTITY_DEBUG(targetEntityID, "Failed to serialize getMilanInfo: {}", e.what());
	}
}

/* Connection Management Protocol (ACMP) */
void CapabilityDelegate::connectStream(model::StreamIdentification const& talkerStream, model::StreamIdentification const& listenerStream, Interface::ConnectStreamHandler const& handler) const noexcept
{
	auto errorCallback = LocalEntityImpl<>::makeACMPErrorHandler(handler, &_controllerInterface, talkerStream, listenerStream, std::uint16_t(0), entity::ConnectionFlags::None, std::placeholders::_1);
	sendAcmpCommand(protocol::AcmpMessageType::ConnectRxCommand, talkerStream.entityID, talkerStream.streamIndex, listenerStream.entityID, listenerStream.streamIndex, std::uint16_t(0), errorCallback, handler);
}

void CapabilityDelegate::disconnectStream(model::StreamIdentification const& talkerStream, model::StreamIdentification const& listenerStream, Interface::DisconnectStreamHandler const& handler) const noexcept
{
	auto errorCallback = LocalEntityImpl<>::makeACMPErrorHandler(handler, &_controllerInterface, talkerStream, listenerStream, std::uint16_t(0), entity::ConnectionFlags::None, std::placeholders::_1);
	sendAcmpCommand(protocol::AcmpMessageType::DisconnectRxCommand, talkerStream.entityID, talkerStream.streamIndex, listenerStream.entityID, listenerStream.streamIndex, std::uint16_t(0), errorCallback, handler);
}

void CapabilityDelegate::disconnectTalkerStream(model::StreamIdentification const& talkerStream, model::StreamIdentification const& listenerStream, Interface::DisconnectTalkerStreamHandler const& handler) const noexcept
{
	auto errorCallback = LocalEntityImpl<>::makeACMPErrorHandler(handler, &_controllerInterface, talkerStream, listenerStream, std::uint16_t(0), entity::ConnectionFlags::None, std::placeholders::_1);
	sendAcmpCommand(protocol::AcmpMessageType::DisconnectTxCommand, talkerStream.entityID, talkerStream.streamIndex, listenerStream.entityID, listenerStream.streamIndex, std::uint16_t(0), errorCallback, handler);
}

void CapabilityDelegate::getTalkerStreamState(model::StreamIdentification const& talkerStream, Interface::GetTalkerStreamStateHandler const& handler) const noexcept
{
	auto errorCallback = LocalEntityImpl<>::makeACMPErrorHandler(handler, &_controllerInterface, talkerStream, model::StreamIdentification{}, std::uint16_t(0), entity::ConnectionFlags::None, std::placeholders::_1);
	sendAcmpCommand(protocol::AcmpMessageType::GetTxStateCommand, talkerStream.entityID, talkerStream.streamIndex, UniqueIdentifier::getNullUniqueIdentifier(), model::StreamIndex(0), std::uint16_t(0), errorCallback, handler);
}

void CapabilityDelegate::getListenerStreamState(model::StreamIdentification const& listenerStream, Interface::GetListenerStreamStateHandler const& handler) const noexcept
{
	auto errorCallback = LocalEntityImpl<>::makeACMPErrorHandler(handler, &_controllerInterface, model::StreamIdentification{}, listenerStream, std::uint16_t(0), entity::ConnectionFlags::None, std::placeholders::_1);
	sendAcmpCommand(protocol::AcmpMessageType::GetRxStateCommand, UniqueIdentifier::getNullUniqueIdentifier(), model::StreamIndex(0), listenerStream.entityID, listenerStream.streamIndex, std::uint16_t(0), errorCallback, handler);
}

void CapabilityDelegate::getTalkerStreamConnection(model::StreamIdentification const& talkerStream, uint16_t const connectionIndex, Interface::GetTalkerStreamConnectionHandler const& handler) const noexcept
{
	auto errorCallback = LocalEntityImpl<>::makeACMPErrorHandler(handler, &_controllerInterface, talkerStream, model::StreamIdentification{}, connectionIndex, entity::ConnectionFlags::None, std::placeholders::_1);
	sendAcmpCommand(protocol::AcmpMessageType::GetTxConnectionCommand, talkerStream.entityID, talkerStream.streamIndex, UniqueIdentifier::getNullUniqueIdentifier(), model::StreamIndex(0), connectionIndex, errorCallback, handler);
}

/* ************************************************************************** */
/* LocalEntityImpl<>::CapabilityDelegate overrides                          */
/* ************************************************************************** */
/* *** General notifications */
void CapabilityDelegate::onControllerDelegateChanged(controller::Delegate* const delegate) noexcept
{
#pragma message("TODO: Protect the _controllerDelegate so it cannot be changed while it's being used (use pi's lock ?? Check for deadlocks!)")
	_controllerDelegate = delegate;
}

//void CapabilityDelegate::onListenerDelegateChanged(listener::Delegate* const delegate) noexcept {}

//void CapabilityDelegate::onTalkerDelegateChanged(talker::Delegate* const delegate) noexcept {}

void CapabilityDelegate::onTransportError(protocol::ProtocolInterface* const /*pi*/) noexcept
{
	utils::invokeProtectedMethod(&controller::Delegate::onTransportError, _controllerDelegate, &_controllerInterface);
}

/* **** Discovery notifications **** */
void CapabilityDelegate::onLocalEntityOnline(protocol::ProtocolInterface* const pi, Entity const& entity) noexcept
{
	// Ignore ourself
	if (entity.getEntityID() == _controllerID)
		return;

	// Forward to RemoteEntityOnline, we handle all discovered entities the same way
	onRemoteEntityOnline(pi, entity);
}

void CapabilityDelegate::onLocalEntityOffline(protocol::ProtocolInterface* const pi, UniqueIdentifier const entityID) noexcept
{
	// Ignore ourself
	if (entityID == _controllerID)
		return;

	// Forward to RemoteEntityOffline, we handle all discovered entities the same way
	onRemoteEntityOffline(pi, entityID);
}

void CapabilityDelegate::onLocalEntityUpdated(protocol::ProtocolInterface* const pi, Entity const& entity) noexcept
{
	// Ignore ourself
	if (entity.getEntityID() == _controllerID)
		return;

	// Forward to RemoteEntityUpdated, we handle all discovered entities the same way
	onRemoteEntityUpdated(pi, entity);
}

void CapabilityDelegate::onRemoteEntityOnline(protocol::ProtocolInterface* const pi, Entity const& entity) noexcept
{
	auto const entityID = entity.getEntityID();
	{
		// Lock ProtocolInterface
		std::lock_guard<decltype(*pi)> const lg(*pi);

		// Store or replace entity
		{
#ifdef __cpp_lib_unordered_map_try_emplace
			AVDECC_ASSERT(_discoveredEntities.find(entityID) == _discoveredEntities.end(), "CapabilityDelegate::onRemoteEntityOnline: Entity already online");
			_discoveredEntities.insert_or_assign(entityID, entity);
#else // !__cpp_lib_unordered_map_try_emplace
			auto it = _discoveredEntities.find(entityID);
			if (AVDECC_ASSERT_WITH_RET(it == _discoveredEntities.end(), "CapabilityDelegate::onRemoteEntityOnline: Entity already online"))
				_discoveredEntities.insert(std::make_pair(entityID, entity));
			else
				it->second = entity;
#endif // __cpp_lib_unordered_map_try_emplace
		}
	}

	utils::invokeProtectedMethod(&controller::Delegate::onEntityOnline, _controllerDelegate, &_controllerInterface, entityID, entity);
}

void CapabilityDelegate::onRemoteEntityOffline(protocol::ProtocolInterface* const pi, UniqueIdentifier const entityID) noexcept
{
	{
		// Lock ProtocolInterface
		std::lock_guard<decltype(*pi)> const lg(*pi);

		// Remove entity
		_discoveredEntities.erase(entityID);
	}

	utils::invokeProtectedMethod(&controller::Delegate::onEntityOffline, _controllerDelegate, &_controllerInterface, entityID);
}

void CapabilityDelegate::onRemoteEntityUpdated(protocol::ProtocolInterface* const pi, Entity const& entity) noexcept
{
	auto const entityID = entity.getEntityID();
	{
		// Lock ProtocolInterface
		std::lock_guard<decltype(*pi)> const lg(*pi);

		// Store or replace entity
		{
#ifdef __cpp_lib_unordered_map_try_emplace
			AVDECC_ASSERT(_discoveredEntities.find(entityID) != _discoveredEntities.end(), "CapabilityDelegate::onRemoteEntityUpdated: Entity offline");
			_discoveredEntities.insert_or_assign(entityID, entity);
#else // !__cpp_lib_unordered_map_try_emplace
			auto it = _discoveredEntities.find(entityID);
			if (!AVDECC_ASSERT_WITH_RET(it != _discoveredEntities.end(), "CapabilityDelegate::onRemoteEntityUpdated: Entity offline"))
				_discoveredEntities.insert(std::make_pair(entityID, entity));
			else
				it->second = entity;
#endif // __cpp_lib_unordered_map_try_emplace
		}
	}

	utils::invokeProtectedMethod(&controller::Delegate::onEntityUpdate, _controllerDelegate, &_controllerInterface, entityID, entity);
}

/* **** AECP notifications **** */
bool CapabilityDelegate::onUnhandledAecpCommand(protocol::ProtocolInterface* const pi, protocol::Aecpdu const& aecpdu) noexcept
{
	if (aecpdu.getMessageType() == protocol::AecpMessageType::AemCommand)
	{
		auto const& aem = static_cast<protocol::AemAecpdu const&>(aecpdu);

		if (!AVDECC_ASSERT_WITH_RET(_controllerID != aecpdu.getControllerEntityID(), "Message from self should not pass through this function, or maybe if the same entity has Controller/Talker/Listener capabilities? (in that case allow the message to be processed, the ProtocolInterface will optimize the sending)"))
			return true;

		if (aem.getCommandType() == protocol::AemCommandType::ControllerAvailable)
		{
			// We are being asked if we are available, and we are! Reply that
			LocalEntityImpl<>::sendAemAecpResponse(pi, aem, protocol::AemAecpStatus::Success, nullptr, 0u);
			return true;
		}
	}
	return false;
}

void CapabilityDelegate::onAecpUnsolicitedResponse(protocol::ProtocolInterface* const /*pi*/, LocalEntity const& /*entity*/, protocol::Aecpdu const& aecpdu) noexcept
{
	// Ignore messages not for me
	if (_controllerID != aecpdu.getControllerEntityID())
		return;

	auto const messageType = aecpdu.getMessageType();

	if (messageType == protocol::AecpMessageType::AemResponse)
	{
		auto const& aem = static_cast<protocol::AemAecpdu const&>(aecpdu);
		if (AVDECC_ASSERT_WITH_RET(aem.getUnsolicited(), "Should only be triggered for unsollicited notifications"))
		{
			// Process AEM message without any error or answer callbacks, it's not an expected response
			processAemAecpResponse(&aecpdu, nullptr, {});
		}
	}
}

/* **** ACMP notifications **** */
void CapabilityDelegate::onAcmpSniffedCommand(protocol::ProtocolInterface* const /*pi*/, LocalEntity const& /*entity*/, protocol::Acmpdu const& /*acmpdu*/) noexcept {}

void CapabilityDelegate::onAcmpSniffedResponse(protocol::ProtocolInterface* const /*pi*/, LocalEntity const& /*entity*/, protocol::Acmpdu const& acmpdu) noexcept
{
	processAcmpResponse(&acmpdu, LocalEntityImpl<>::OnACMPErrorCallback(), LocalEntityImpl<>::AnswerCallback(), true);
}

/* ************************************************************************** */
/* Internal methods                                                           */
/* ************************************************************************** */
void CapabilityDelegate::sendAemAecpCommand(UniqueIdentifier const targetEntityID, protocol::AemCommandType const commandType, void const* const payload, size_t const payloadLength, LocalEntityImpl<>::OnAemAECPErrorCallback const& onErrorCallback, LocalEntityImpl<>::AnswerCallback const& answerCallback) const noexcept
{
	auto targetMacAddress = networkInterface::MacAddress{};

	// Search target mac address based on its entityID
	{
		// Lock ProtocolInterface
		std::lock_guard<decltype(*_protocolInterface)> const lg(*_protocolInterface);

		auto const it = _discoveredEntities.find(targetEntityID);

		if (it != _discoveredEntities.end())
		{
			// Get entity mac address
			targetMacAddress = it->second.getAnyMacAddress();
		}
	}

	// Return an error if entity is not found in the list
	if (!networkInterface::isMacAddressValid(targetMacAddress))
	{
		utils::invokeProtectedHandler(onErrorCallback, LocalEntity::AemCommandStatus::UnknownEntity);
		return;
	}

	LocalEntityImpl<>::sendAemAecpCommand(_protocolInterface, _controllerID, targetEntityID, targetMacAddress, commandType, payload, payloadLength,
		[this, onErrorCallback, answerCallback](protocol::Aecpdu const* const response, LocalEntity::AemCommandStatus const status)
		{
			if (!!status)
			{
				processAemAecpResponse(response, onErrorCallback, answerCallback); // We sent an AEM command, we know it's an AEM response (so directly call processAemAecpResponse)
			}
			else
			{
				utils::invokeProtectedHandler(onErrorCallback, status);
			}
		});
}

void CapabilityDelegate::sendAaAecpCommand(UniqueIdentifier const targetEntityID, addressAccess::Tlvs const& tlvs, LocalEntityImpl<>::OnAaAECPErrorCallback const& onErrorCallback, LocalEntityImpl<>::AnswerCallback const& answerCallback) const noexcept
{
	auto targetMacAddress = networkInterface::MacAddress{};

	// Search target mac address based on its entityID
	{
		// Lock ProtocolInterface
		std::lock_guard<decltype(*_protocolInterface)> const lg(*_protocolInterface);

		auto const it = _discoveredEntities.find(targetEntityID);

		if (it != _discoveredEntities.end())
		{
			// Get entity mac address
			targetMacAddress = it->second.getAnyMacAddress();
		}
	}

	// Return an error if entity is not found in the list
	if (!networkInterface::isMacAddressValid(targetMacAddress))
	{
		utils::invokeProtectedHandler(onErrorCallback, LocalEntity::AaCommandStatus::UnknownEntity);
		return;
	}

	LocalEntityImpl<>::sendAaAecpCommand(_protocolInterface, _controllerID, targetEntityID, targetMacAddress, tlvs,
		[this, onErrorCallback, answerCallback](protocol::Aecpdu const* const response, LocalEntity::AaCommandStatus const status)
		{
			if (!!status)
			{
				processAaAecpResponse(response, onErrorCallback, answerCallback); // We sent an Address Access command, we know it's an Address Access response (so directly call processAaAecpResponse)
			}
			else
			{
				utils::invokeProtectedHandler(onErrorCallback, status);
			}
		});
}

void CapabilityDelegate::sendMvuAecpCommand(UniqueIdentifier const targetEntityID, protocol::MvuCommandType const commandType, void const* const payload, size_t const payloadLength, LocalEntityImpl<>::OnMvuAECPErrorCallback const& onErrorCallback, LocalEntityImpl<>::AnswerCallback const& answerCallback) const noexcept
{
	auto targetMacAddress = networkInterface::MacAddress{};

	// Search target mac address based on its entityID
	{
		// Lock ProtocolInterface
		std::lock_guard<decltype(*_protocolInterface)> const lg(*_protocolInterface);

		auto const it = _discoveredEntities.find(targetEntityID);

		if (it != _discoveredEntities.end())
		{
			// Get entity mac address
			targetMacAddress = it->second.getAnyMacAddress();
		}
	}

	// Return an error if entity is not found in the list
	if (!networkInterface::isMacAddressValid(targetMacAddress))
	{
		utils::invokeProtectedHandler(onErrorCallback, LocalEntity::MvuCommandStatus::UnknownEntity);
		return;
	}

	LocalEntityImpl<>::sendMvuAecpCommand(_protocolInterface, _controllerID, targetEntityID, targetMacAddress, commandType, payload, payloadLength,
		[this, onErrorCallback, answerCallback](protocol::Aecpdu const* const response, LocalEntity::MvuCommandStatus const status)
		{
			if (!!status)
			{
				processMvuAecpResponse(response, onErrorCallback, answerCallback); // We sent an MVU command, we know it's an MVU response (so directly call processMvuAecpResponse)
			}
			else
			{
				utils::invokeProtectedHandler(onErrorCallback, status);
			}
		});
}

void CapabilityDelegate::sendAcmpCommand(protocol::AcmpMessageType const messageType, UniqueIdentifier const talkerEntityID, model::StreamIndex const talkerStreamIndex, UniqueIdentifier const listenerEntityID, model::StreamIndex const listenerStreamIndex, uint16_t const connectionIndex, LocalEntityImpl<>::OnACMPErrorCallback const& onErrorCallback, LocalEntityImpl<>::AnswerCallback const& answerCallback) const noexcept
{
	LocalEntityImpl<>::sendAcmpCommand(_protocolInterface, messageType, _controllerID, talkerEntityID, talkerStreamIndex, listenerEntityID, listenerStreamIndex, connectionIndex,
		[this, onErrorCallback, answerCallback](protocol::Acmpdu const* const response, LocalEntity::ControlStatus const status)
		{
			if (!!status)
			{
				processAcmpResponse(response, onErrorCallback, answerCallback, false);
			}
			else
			{
				utils::invokeProtectedHandler(onErrorCallback, status);
			}
		});
}

void CapabilityDelegate::processAemAecpResponse(protocol::Aecpdu const* const response, LocalEntityImpl<>::OnAemAECPErrorCallback const& onErrorCallback, LocalEntityImpl<>::AnswerCallback const& answerCallback) const noexcept
{
	auto const& aem = static_cast<protocol::AemAecpdu const&>(*response);
	auto const status = static_cast<LocalEntity::AemCommandStatus>(aem.getStatus().getValue()); // We have to convert protocol status to our extended status

	static std::unordered_map<protocol::AemCommandType::value_type, std::function<void(controller::Delegate* const delegate, Interface const* const controllerInterface, LocalEntity::AemCommandStatus const status, protocol::AemAecpdu const& aem, LocalEntityImpl<>::AnswerCallback const& answerCallback)>> s_Dispatch
	{
		// Acquire Entity
		{ protocol::AemCommandType::AcquireEntity.getValue(), [](controller::Delegate* const delegate, Interface const* const controllerInterface, LocalEntity::AemCommandStatus const status, protocol::AemAecpdu const& aem, LocalEntityImpl<>::AnswerCallback const& answerCallback)
			{
	// Deserialize payload
#ifdef __cpp_structured_bindings
				auto const[flags, ownerID, descriptorType, descriptorIndex] = protocol::aemPayload::deserializeAcquireEntityResponse(aem.getPayload());
#else // !__cpp_structured_bindings
				auto const result = protocol::aemPayload::deserializeAcquireEntityResponse(aem.getPayload());
				protocol::AemAcquireEntityFlags const flags = std::get<0>(result);
				UniqueIdentifier const ownerID = std::get<1>(result);
				entity::model::DescriptorType const descriptorType = std::get<2>(result);
				entity::model::DescriptorIndex const descriptorIndex = std::get<3>(result);
#endif // __cpp_structured_bindings

				auto const targetID = aem.getTargetEntityID();

				if ((flags & protocol::AemAcquireEntityFlags::Release) == protocol::AemAcquireEntityFlags::Release)
				{
					answerCallback.invoke<controller::Interface::ReleaseEntityHandler>(controllerInterface, targetID, status, ownerID, descriptorType, descriptorIndex);
					if (aem.getUnsolicited() && delegate && !!status)
					{
						utils::invokeProtectedMethod(&controller::Delegate::onEntityReleased, delegate, controllerInterface, targetID, ownerID, descriptorType, descriptorIndex);
					}
				}
				else
				{
					answerCallback.invoke<controller::Interface::AcquireEntityHandler>(controllerInterface, targetID, status, ownerID, descriptorType, descriptorIndex);
					if (aem.getUnsolicited() && delegate && !!status)
					{
						utils::invokeProtectedMethod(&controller::Delegate::onEntityAcquired, delegate, controllerInterface, targetID, ownerID, descriptorType, descriptorIndex);
					}
				}
			}
		},
		// Lock Entity
		{ protocol::AemCommandType::LockEntity.getValue(), [](controller::Delegate* const delegate, Interface const* const controllerInterface, LocalEntity::AemCommandStatus const status, protocol::AemAecpdu const& aem, LocalEntityImpl<>::AnswerCallback const& answerCallback)
			{
	// Deserialize payload
#ifdef __cpp_structured_bindings
				auto const[flags, lockedID, descriptorType, descriptorIndex] = protocol::aemPayload::deserializeLockEntityResponse(aem.getPayload());
#else // !__cpp_structured_bindings
				auto const result = protocol::aemPayload::deserializeLockEntityResponse(aem.getPayload());
				protocol::AemLockEntityFlags const flags = std::get<0>(result);
				UniqueIdentifier const lockedID = std::get<1>(result);
				entity::model::DescriptorType const descriptorType = std::get<2>(result);
				entity::model::DescriptorIndex const descriptorIndex = std::get<3>(result);
#endif // __cpp_structured_bindings

				auto const targetID = aem.getTargetEntityID();

				if ((flags & protocol::AemLockEntityFlags::Unlock) == protocol::AemLockEntityFlags::Unlock)
				{
					answerCallback.invoke<controller::Interface::UnlockEntityHandler>(controllerInterface, targetID, status, lockedID, descriptorType, descriptorIndex);
					if (aem.getUnsolicited() && delegate && !!status)
					{
						utils::invokeProtectedMethod(&controller::Delegate::onEntityUnlocked, delegate, controllerInterface, targetID, lockedID, descriptorType, descriptorIndex);
					}
				}
				else
				{
					answerCallback.invoke<controller::Interface::LockEntityHandler>(controllerInterface, targetID, status, lockedID, descriptorType, descriptorIndex);
					if (aem.getUnsolicited() && delegate && !!status)
					{
						utils::invokeProtectedMethod(&controller::Delegate::onEntityLocked, delegate, controllerInterface, targetID, lockedID, descriptorType, descriptorIndex);
					}
				}
			}
		},
		// Entity Available
		{ protocol::AemCommandType::EntityAvailable.getValue(), [](controller::Delegate* const /*delegate*/, Interface const* const controllerInterface, LocalEntity::AemCommandStatus const status, protocol::AemAecpdu const& aem, LocalEntityImpl<>::AnswerCallback const& answerCallback)
			{
				auto const targetID = aem.getTargetEntityID();
				answerCallback.invoke<controller::Interface::QueryEntityAvailableHandler>(controllerInterface, targetID, status);
			}
		},
		// Controller Available
		{ protocol::AemCommandType::ControllerAvailable.getValue(), [](controller::Delegate* const /*delegate*/, Interface const* const controllerInterface, LocalEntity::AemCommandStatus const status, protocol::AemAecpdu const& aem, LocalEntityImpl<>::AnswerCallback const& answerCallback)
			{
				auto const targetID = aem.getTargetEntityID();
				answerCallback.invoke<controller::Interface::QueryControllerAvailableHandler>(controllerInterface, targetID, status);
			}
		},
		// Read Descriptor
		{ protocol::AemCommandType::ReadDescriptor.getValue(), [](controller::Delegate* const /*delegate*/, Interface const* const controllerInterface, LocalEntity::AemCommandStatus const status, protocol::AemAecpdu const& aem, LocalEntityImpl<>::AnswerCallback const& answerCallback)
			{
				auto const payload = aem.getPayload();
		// Deserialize payload
#ifdef __cpp_structured_bindings
				auto const[commonSize, configurationIndex, descriptorType, descriptorIndex] = protocol::aemPayload::deserializeReadDescriptorCommonResponse(payload);
#else // !__cpp_structured_bindings
				auto const result = protocol::aemPayload::deserializeReadDescriptorCommonResponse(payload);
				size_t const commonSize = std::get<0>(result);
				entity::model::ConfigurationIndex const configurationIndex = std::get<1>(result);
				entity::model::DescriptorType const descriptorType = std::get<2>(result);
				entity::model::DescriptorIndex const descriptorIndex = std::get<3>(result);
#endif // __cpp_structured_bindings

				auto const targetID = aem.getTargetEntityID();
				auto const aemStatus = protocol::AemAecpStatus(static_cast<protocol::AemAecpStatus::value_type>(status));

				switch (descriptorType)
				{
					case model::DescriptorType::Entity:
					{
						// Deserialize entity descriptor
						auto entityDescriptor = protocol::aemPayload::deserializeReadEntityDescriptorResponse(payload, commonSize, aemStatus);
						// Notify handlers
						answerCallback.invoke<controller::Interface::EntityDescriptorHandler>(controllerInterface, targetID, status, entityDescriptor);
						break;
					}

					case model::DescriptorType::Configuration:
					{
						// Deserialize configuration descriptor
						auto configurationDescriptor = protocol::aemPayload::deserializeReadConfigurationDescriptorResponse(payload, commonSize, aemStatus);
						// Notify handlers
						answerCallback.invoke<controller::Interface::ConfigurationDescriptorHandler>(controllerInterface, targetID, status, static_cast<model::ConfigurationIndex>(descriptorIndex), configurationDescriptor); // Passing descriptorIndex as ConfigurationIndex here is NOT an error. See 7.4.5.1
						break;
					}

					case model::DescriptorType::AudioUnit:
					{
						// Deserialize audio unit descriptor
						auto audioUnitDescriptor = protocol::aemPayload::deserializeReadAudioUnitDescriptorResponse(payload, commonSize, aemStatus);
						// Notify handlers
						answerCallback.invoke<controller::Interface::AudioUnitDescriptorHandler>(controllerInterface, targetID, status, configurationIndex, descriptorIndex, audioUnitDescriptor);
						break;
					}

					case model::DescriptorType::StreamInput:
					{
						// Deserialize stream input descriptor
						auto streamDescriptor = protocol::aemPayload::deserializeReadStreamDescriptorResponse(payload, commonSize, aemStatus);
						// Notify handlers
						answerCallback.invoke<controller::Interface::StreamInputDescriptorHandler>(controllerInterface, targetID, status, configurationIndex, descriptorIndex, streamDescriptor);
						break;
					}

					case model::DescriptorType::StreamOutput:
					{
						// Deserialize stream output descriptor
						auto streamDescriptor = protocol::aemPayload::deserializeReadStreamDescriptorResponse(payload, commonSize, aemStatus);
						// Notify handlers
						answerCallback.invoke<controller::Interface::StreamOutputDescriptorHandler>(controllerInterface, targetID, status, configurationIndex, descriptorIndex, streamDescriptor);
						break;
					}

					case model::DescriptorType::JackInput:
					{
						// Deserialize jack input descriptor
						auto jackDescriptor = protocol::aemPayload::deserializeReadJackDescriptorResponse(payload, commonSize, aemStatus);
						// Notify handlers
						answerCallback.invoke<controller::Interface::JackInputDescriptorHandler>(controllerInterface, targetID, status, configurationIndex, descriptorIndex, jackDescriptor);
						break;
					}

					case model::DescriptorType::JackOutput:
					{
						// Deserialize jack output descriptor
						auto jackDescriptor = protocol::aemPayload::deserializeReadJackDescriptorResponse(payload, commonSize, aemStatus);
						// Notify handlers
						answerCallback.invoke<controller::Interface::JackOutputDescriptorHandler>(controllerInterface, targetID, status, configurationIndex, descriptorIndex, jackDescriptor);
						break;
					}

					case model::DescriptorType::AvbInterface:
					{
						// Deserialize avb interface descriptor
						auto avbInterfaceDescriptor = protocol::aemPayload::deserializeReadAvbInterfaceDescriptorResponse(payload, commonSize, aemStatus);
						// Notify handlers
						answerCallback.invoke<controller::Interface::AvbInterfaceDescriptorHandler>(controllerInterface, targetID, status, configurationIndex, descriptorIndex, avbInterfaceDescriptor);
						break;
					}

					case model::DescriptorType::ClockSource:
					{
						// Deserialize clock source descriptor
						auto clockSourceDescriptor = protocol::aemPayload::deserializeReadClockSourceDescriptorResponse(payload, commonSize, aemStatus);
						// Notify handlers
						answerCallback.invoke<controller::Interface::ClockSourceDescriptorHandler>(controllerInterface, targetID, status, configurationIndex, descriptorIndex, clockSourceDescriptor);
						break;
					}

					case model::DescriptorType::MemoryObject:
					{
						// Deserialize memory object descriptor
						auto memoryObjectDescriptor = protocol::aemPayload::deserializeReadMemoryObjectDescriptorResponse(payload, commonSize, aemStatus);
						// Notify handlers
						answerCallback.invoke<controller::Interface::MemoryObjectDescriptorHandler>(controllerInterface, targetID, status, configurationIndex, descriptorIndex, memoryObjectDescriptor);
						break;
					}

					case model::DescriptorType::Locale:
					{
						// Deserialize locale descriptor
						auto localeDescriptor = protocol::aemPayload::deserializeReadLocaleDescriptorResponse(payload, commonSize, aemStatus);
						// Notify handlers
						answerCallback.invoke<controller::Interface::LocaleDescriptorHandler>(controllerInterface, targetID, status, configurationIndex, descriptorIndex, localeDescriptor);
						break;
					}

					case model::DescriptorType::Strings:
					{
						// Deserialize strings descriptor
						auto stringsDescriptor = protocol::aemPayload::deserializeReadStringsDescriptorResponse(payload, commonSize, aemStatus);
						// Notify handlers
						answerCallback.invoke<controller::Interface::StringsDescriptorHandler>(controllerInterface, targetID, status, configurationIndex, descriptorIndex, stringsDescriptor);
						break;
					}

					case model::DescriptorType::StreamPortInput:
					{
						// Deserialize stream port descriptor
						auto streamPortDescriptor = protocol::aemPayload::deserializeReadStreamPortDescriptorResponse(payload, commonSize, aemStatus);
						// Notify handlers
						answerCallback.invoke<controller::Interface::StreamPortInputDescriptorHandler>(controllerInterface, targetID, status, configurationIndex, descriptorIndex, streamPortDescriptor);
						break;
					}

					case model::DescriptorType::StreamPortOutput:
					{
						// Deserialize stream port descriptor
						auto streamPortDescriptor = protocol::aemPayload::deserializeReadStreamPortDescriptorResponse(payload, commonSize, aemStatus);
						// Notify handlers
						answerCallback.invoke<controller::Interface::StreamPortOutputDescriptorHandler>(controllerInterface, targetID, status, configurationIndex, descriptorIndex, streamPortDescriptor);
						break;
					}

					case model::DescriptorType::ExternalPortInput:
					{
						// Deserialize external port descriptor
						auto externalPortDescriptor = protocol::aemPayload::deserializeReadExternalPortDescriptorResponse(payload, commonSize, aemStatus);
						// Notify handlers
						answerCallback.invoke<controller::Interface::ExternalPortInputDescriptorHandler>(controllerInterface, targetID, status, configurationIndex, descriptorIndex, externalPortDescriptor);
						break;
					}

					case model::DescriptorType::ExternalPortOutput:
					{
						// Deserialize external port descriptor
						auto externalPortDescriptor = protocol::aemPayload::deserializeReadExternalPortDescriptorResponse(payload, commonSize, aemStatus);
						// Notify handlers
						answerCallback.invoke<controller::Interface::ExternalPortOutputDescriptorHandler>(controllerInterface, targetID, status, configurationIndex, descriptorIndex, externalPortDescriptor);
						break;
					}

					case model::DescriptorType::InternalPortInput:
					{
						// Deserialize internal port descriptor
						auto internalPortDescriptor = protocol::aemPayload::deserializeReadInternalPortDescriptorResponse(payload, commonSize, aemStatus);
						// Notify handlers
						answerCallback.invoke<controller::Interface::InternalPortInputDescriptorHandler>(controllerInterface, targetID, status, configurationIndex, descriptorIndex, internalPortDescriptor);
						break;
					}

					case model::DescriptorType::InternalPortOutput:
					{
						// Deserialize internal port descriptor
						auto internalPortDescriptor = protocol::aemPayload::deserializeReadInternalPortDescriptorResponse(payload, commonSize, aemStatus);
						// Notify handlers
						answerCallback.invoke<controller::Interface::InternalPortOutputDescriptorHandler>(controllerInterface, targetID, status, configurationIndex, descriptorIndex, internalPortDescriptor);
						break;
					}

					case model::DescriptorType::AudioCluster:
					{
						// Deserialize audio cluster descriptor
						auto audioClusterDescriptor = protocol::aemPayload::deserializeReadAudioClusterDescriptorResponse(payload, commonSize, aemStatus);
						// Notify handlers
						answerCallback.invoke<controller::Interface::AudioClusterDescriptorHandler>(controllerInterface, targetID, status, configurationIndex, descriptorIndex, audioClusterDescriptor);
						break;
					}

					case model::DescriptorType::AudioMap:
					{
						// Deserialize audio map descriptor
						auto audioMapDescriptor = protocol::aemPayload::deserializeReadAudioMapDescriptorResponse(payload, commonSize, aemStatus);
						// Notify handlers
						answerCallback.invoke<controller::Interface::AudioMapDescriptorHandler>(controllerInterface, targetID, status, configurationIndex, descriptorIndex, audioMapDescriptor);
						break;
					}

					case model::DescriptorType::ClockDomain:
					{
						// Deserialize clock domain descriptor
						auto clockDomainDescriptor = protocol::aemPayload::deserializeReadClockDomainDescriptorResponse(payload, commonSize, aemStatus);
						// Notify handlers
						answerCallback.invoke<controller::Interface::ClockDomainDescriptorHandler>(controllerInterface, targetID, status, configurationIndex, descriptorIndex, clockDomainDescriptor);
						break;
					}

					default:
						AVDECC_ASSERT(false, "Unhandled descriptor type");
						break;
				}
			}
		},
		// Write Descriptor
		// Set Configuration
		{ protocol::AemCommandType::SetConfiguration.getValue(), [](controller::Delegate* const delegate, Interface const* const controllerInterface, LocalEntity::AemCommandStatus const status, protocol::AemAecpdu const& aem, LocalEntityImpl<>::AnswerCallback const& answerCallback)
			{
	// Deserialize payload
#ifdef __cpp_structured_bindings
				auto const[configurationIndex] = protocol::aemPayload::deserializeSetConfigurationResponse(aem.getPayload());
#else // !__cpp_structured_bindings
				auto const result = protocol::aemPayload::deserializeSetConfigurationResponse(aem.getPayload());
				model::ConfigurationIndex const configurationIndex = std::get<0>(result);
#endif // __cpp_structured_bindings

				auto const targetID = aem.getTargetEntityID();

				// Notify handlers
				answerCallback.invoke<controller::Interface::SetConfigurationHandler>(controllerInterface, targetID, status, configurationIndex);
				if (aem.getUnsolicited() && delegate && !!status)
				{
					utils::invokeProtectedMethod(&controller::Delegate::onConfigurationChanged, delegate, controllerInterface, targetID, configurationIndex);
				}
			}
		},
		// Get Configuration
		// Set Stream Format
		{ protocol::AemCommandType::SetStreamFormat.getValue(), [](controller::Delegate* const delegate, Interface const* const controllerInterface, LocalEntity::AemCommandStatus const status, protocol::AemAecpdu const& aem, LocalEntityImpl<>::AnswerCallback const& answerCallback)
			{
	// Deserialize payload
#ifdef __cpp_structured_bindings
				auto const[descriptorType, descriptorIndex, streamFormat] = protocol::aemPayload::deserializeSetStreamFormatResponse(aem.getPayload());
#else // !__cpp_structured_bindings
				auto const result = protocol::aemPayload::deserializeSetStreamFormatResponse(aem.getPayload());
				model::DescriptorType const descriptorType = std::get<0>(result);
				model::DescriptorIndex const descriptorIndex = std::get<1>(result);
				entity::model::StreamFormat const streamFormat = std::get<2>(result);
#endif // __cpp_structured_bindings

				auto const targetID = aem.getTargetEntityID();

				// Notify handlers
				if (descriptorType == model::DescriptorType::StreamInput)
				{
					answerCallback.invoke<controller::Interface::SetStreamInputFormatHandler>(controllerInterface, targetID, status, descriptorIndex, streamFormat);
					if (aem.getUnsolicited() && delegate && !!status)
					{
						utils::invokeProtectedMethod(&controller::Delegate::onStreamInputFormatChanged, delegate, controllerInterface, targetID, descriptorIndex, streamFormat);
					}
				}
				else if (descriptorType == model::DescriptorType::StreamOutput)
				{
					answerCallback.invoke<controller::Interface::SetStreamOutputFormatHandler>(controllerInterface, targetID, status, descriptorIndex, streamFormat);
					if (aem.getUnsolicited() && delegate && !!status)
					{
						utils::invokeProtectedMethod(&controller::Delegate::onStreamOutputFormatChanged, delegate, controllerInterface, targetID, descriptorIndex, streamFormat);
					}
				}
				else
					throw InvalidDescriptorTypeException();
			}
		},
		// Get Stream Format
		{ protocol::AemCommandType::GetStreamFormat.getValue(), [](controller::Delegate* const /*delegate*/, Interface const* const controllerInterface, LocalEntity::AemCommandStatus const status, protocol::AemAecpdu const& aem, LocalEntityImpl<>::AnswerCallback const& answerCallback)
			{
	// Deserialize payload
#ifdef __cpp_structured_bindings
				auto const[descriptorType, descriptorIndex, streamFormat] = protocol::aemPayload::deserializeGetStreamFormatResponse(aem.getPayload());
#else // !__cpp_structured_bindings
				auto const result = protocol::aemPayload::deserializeGetStreamFormatResponse(aem.getPayload());
				model::DescriptorType const descriptorType = std::get<0>(result);
				model::DescriptorIndex const descriptorIndex = std::get<1>(result);
				entity::model::StreamFormat const streamFormat = std::get<2>(result);
#endif // __cpp_structured_bindings

				auto const targetID = aem.getTargetEntityID();

				// Notify handlers
				if (descriptorType == model::DescriptorType::StreamInput)
				{
					answerCallback.invoke<controller::Interface::GetStreamInputFormatHandler>(controllerInterface, targetID, status, descriptorIndex, streamFormat);
				}
				else if (descriptorType == model::DescriptorType::StreamOutput)
				{
					answerCallback.invoke<controller::Interface::GetStreamOutputFormatHandler>(controllerInterface, targetID, status, descriptorIndex, streamFormat);
				}
				else
					throw InvalidDescriptorTypeException();
			}
		},
		// Set Stream Info
		{ protocol::AemCommandType::SetStreamInfo.getValue(), [](controller::Delegate* const delegate, Interface const* const controllerInterface, LocalEntity::AemCommandStatus const status, protocol::AemAecpdu const& aem, LocalEntityImpl<>::AnswerCallback const& answerCallback)
			{
	// Deserialize payload
#ifdef __cpp_structured_bindings
				auto const[descriptorType, descriptorIndex, streamInfo] = protocol::aemPayload::deserializeSetStreamInfoResponse(aem.getPayload());
#else // !__cpp_structured_bindings
				auto const result = protocol::aemPayload::deserializeSetStreamInfoResponse(aem.getPayload());
				model::DescriptorType const descriptorType = std::get<0>(result);
				model::DescriptorIndex const descriptorIndex = std::get<1>(result);
				entity::model::StreamInfo const streamInfo = std::get<2>(result);
#endif // __cpp_structured_bindings

				auto const targetID = aem.getTargetEntityID();

				// Notify handlers
				if (descriptorType == model::DescriptorType::StreamInput)
				{
					answerCallback.invoke<controller::Interface::SetStreamInputInfoHandler>(controllerInterface, targetID, status, descriptorIndex, streamInfo);
					if (aem.getUnsolicited() && delegate && !!status)
					{
						utils::invokeProtectedMethod(&controller::Delegate::onStreamInputInfoChanged, delegate, controllerInterface, targetID, descriptorIndex, streamInfo, false);
					}
				}
				else if (descriptorType == model::DescriptorType::StreamOutput)
				{
					answerCallback.invoke<controller::Interface::SetStreamOutputInfoHandler>(controllerInterface, targetID, status, descriptorIndex, streamInfo);
					if (aem.getUnsolicited() && delegate && !!status)
					{
						utils::invokeProtectedMethod(&controller::Delegate::onStreamOutputInfoChanged, delegate, controllerInterface, targetID, descriptorIndex, streamInfo, false);
					}
				}
				else
					throw InvalidDescriptorTypeException();
			}
		},
		// Get Stream Info
		{ protocol::AemCommandType::GetStreamInfo.getValue(), [](controller::Delegate* const delegate, Interface const* const controllerInterface, LocalEntity::AemCommandStatus const status, protocol::AemAecpdu const& aem, LocalEntityImpl<>::AnswerCallback const& answerCallback)
			{
	// Deserialize payload
#ifdef __cpp_structured_bindings
				auto const[descriptorType, descriptorIndex, streamInfo] = protocol::aemPayload::deserializeGetStreamInfoResponse(aem.getPayload());
#else // !__cpp_structured_bindings
				auto const result = protocol::aemPayload::deserializeGetStreamInfoResponse(aem.getPayload());
				model::DescriptorType const descriptorType = std::get<0>(result);
				model::DescriptorIndex const descriptorIndex = std::get<1>(result);
				entity::model::StreamInfo const streamInfo = std::get<2>(result);
#endif // __cpp_structured_bindings

				auto const targetID = aem.getTargetEntityID();

				// Notify handlers
				if (descriptorType == model::DescriptorType::StreamInput)
				{
					answerCallback.invoke<controller::Interface::GetStreamInputInfoHandler>(controllerInterface, targetID, status, descriptorIndex, streamInfo);
					if (aem.getUnsolicited() && delegate && !!status) // Unsolicited triggered by change in the SRP domain (Clause 7.5.2)
					{
						utils::invokeProtectedMethod(&controller::Delegate::onStreamInputInfoChanged, delegate, controllerInterface, targetID, descriptorIndex, streamInfo, true);
					}
				}
				else if (descriptorType == model::DescriptorType::StreamOutput)
				{
					answerCallback.invoke<controller::Interface::GetStreamOutputInfoHandler>(controllerInterface, targetID, status, descriptorIndex, streamInfo);
					if (aem.getUnsolicited() && delegate && !!status) // Unsolicited triggered by change in the SRP domain (Clause 7.5.2)
					{
						utils::invokeProtectedMethod(&controller::Delegate::onStreamOutputInfoChanged, delegate, controllerInterface, targetID, descriptorIndex, streamInfo, true);
					}
				}
				else
					throw InvalidDescriptorTypeException();
			}
		},
		// Set Name
		{ protocol::AemCommandType::SetName.getValue(), [](controller::Delegate* const delegate, Interface const* const controllerInterface, LocalEntity::AemCommandStatus const status, protocol::AemAecpdu const& aem, LocalEntityImpl<>::AnswerCallback const& answerCallback)
			{
	// Deserialize payload
#ifdef __cpp_structured_bindings
				auto const[descriptorType, descriptorIndex, nameIndex, configurationIndex, name] = protocol::aemPayload::deserializeSetNameResponse(aem.getPayload());
#else // !__cpp_structured_bindings
				auto const result = protocol::aemPayload::deserializeSetNameResponse(aem.getPayload());
				model::DescriptorType const descriptorType = std::get<0>(result);
				model::DescriptorIndex const descriptorIndex = std::get<1>(result);
				std::uint16_t const nameIndex = std::get<2>(result);
				model::ConfigurationIndex const configurationIndex = std::get<3>(result);
				model::AvdeccFixedString const name = std::get<4>(result);
#endif // __cpp_structured_bindings

				auto const targetID = aem.getTargetEntityID();

				// Notify handlers
				switch (descriptorType)
				{
					case model::DescriptorType::Entity:
					{
						if (descriptorIndex != 0)
						{
							LOG_CONTROLLER_ENTITY_DEBUG(targetID, "Invalid descriptorIndex in SET_NAME response for Entity Descriptor: {}", descriptorIndex);
						}
						if (configurationIndex != 0)
						{
							LOG_CONTROLLER_ENTITY_DEBUG(targetID, "Invalid configurationIndex in SET_NAME response for Entity Descriptor: {}", configurationIndex);
						}
						switch (nameIndex)
						{
							case 0: // entity_name
								answerCallback.invoke<controller::Interface::SetEntityNameHandler>(controllerInterface, targetID, status);
								if (aem.getUnsolicited() && delegate && !!status)
								{
									utils::invokeProtectedMethod(&controller::Delegate::onEntityNameChanged, delegate, controllerInterface, targetID, name);
								}
								break;
							case 1: // group_name
								answerCallback.invoke<controller::Interface::SetEntityGroupNameHandler>(controllerInterface, targetID, status);
								if (aem.getUnsolicited() && delegate && !!status)
								{
									utils::invokeProtectedMethod(&controller::Delegate::onEntityGroupNameChanged, delegate, controllerInterface, targetID, name);
								}
								break;
							default:
								LOG_CONTROLLER_ENTITY_DEBUG(targetID, "Unhandled nameIndex in SET_NAME response for Entity Descriptor: DescriptorType={} DescriptorIndex={} NameIndex={} ConfigurationIndex={} Name={}", utils::to_integral(descriptorType), descriptorIndex, nameIndex, configurationIndex, name.str());
								break;
						}
						break;
					}
					case model::DescriptorType::Configuration:
					{
						if (configurationIndex != 0)
						{
							LOG_CONTROLLER_ENTITY_DEBUG(targetID, "Invalid configurationIndex in SET_NAME response for Configuration Descriptor: ConfigurationIndex={}", configurationIndex);
						}
						switch (nameIndex)
						{
							case 0: // object_name
								answerCallback.invoke<controller::Interface::SetConfigurationNameHandler>(controllerInterface, targetID, status, descriptorIndex);
								if (aem.getUnsolicited() && delegate && !!status)
								{
									utils::invokeProtectedMethod(&controller::Delegate::onConfigurationNameChanged, delegate, controllerInterface, targetID, descriptorIndex, name);
								}
								break;
							default:
								LOG_CONTROLLER_ENTITY_DEBUG(targetID, "Unhandled nameIndex in SET_NAME response for Configuration Descriptor: DescriptorType={} DescriptorIndex={} NameIndex={} ConfigurationIndex={} Name={}", utils::to_integral(descriptorType), descriptorIndex, nameIndex, configurationIndex, name.str());
								break;
						}
						break;
					}
					case model::DescriptorType::AudioUnit:
					{
						switch (nameIndex)
						{
							case 0: // object_name
								answerCallback.invoke<controller::Interface::SetAudioUnitNameHandler>(controllerInterface, targetID, status, configurationIndex, descriptorIndex);
								if (aem.getUnsolicited() && delegate && !!status)
								{
									utils::invokeProtectedMethod(&controller::Delegate::onAudioUnitNameChanged, delegate, controllerInterface, targetID, configurationIndex, descriptorIndex, name);
								}
								break;
							default:
								LOG_CONTROLLER_ENTITY_DEBUG(targetID, "Unhandled nameIndex in SET_NAME response for AudioUnit Descriptor: DescriptorType={} DescriptorIndex={} NameIndex={} ConfigurationIndex={} Name={}", utils::to_integral(descriptorType), descriptorIndex, nameIndex, configurationIndex, name.str());
								break;
						}
						break;
					}
					case model::DescriptorType::StreamInput:
					{
						switch (nameIndex)
						{
							case 0: // stream_name
								answerCallback.invoke<controller::Interface::SetStreamInputNameHandler>(controllerInterface, targetID, status, configurationIndex, descriptorIndex);
								if (aem.getUnsolicited() && delegate && !!status)
								{
									utils::invokeProtectedMethod(&controller::Delegate::onStreamInputNameChanged, delegate, controllerInterface, targetID, configurationIndex, descriptorIndex, name);
								}
								break;
							default:
								LOG_CONTROLLER_ENTITY_DEBUG(targetID, "Unhandled nameIndex in SET_NAME response for StreamInput Descriptor: DescriptorType={} DescriptorIndex={} NameIndex={} ConfigurationIndex={} Name={}", utils::to_integral(descriptorType), descriptorIndex, nameIndex, configurationIndex, name.str());
								break;
						}
						break;
					}
					case model::DescriptorType::StreamOutput:
					{
						switch (nameIndex)
						{
							case 0: // stream_name
								answerCallback.invoke<controller::Interface::SetStreamOutputNameHandler>(controllerInterface, targetID, status, configurationIndex, descriptorIndex);
								if (aem.getUnsolicited() && delegate && !!status)
								{
									utils::invokeProtectedMethod(&controller::Delegate::onStreamOutputNameChanged, delegate, controllerInterface, targetID, configurationIndex, descriptorIndex, name);
								}
								break;
							default:
								LOG_CONTROLLER_ENTITY_DEBUG(targetID, "Unhandled nameIndex in SET_NAME response for StreamOutput Descriptor: DescriptorType={} DescriptorIndex={} NameIndex={} ConfigurationIndex={} Name={}", utils::to_integral(descriptorType), descriptorIndex, nameIndex, configurationIndex, name.str());
								break;
						}
						break;
					}
					case model::DescriptorType::AvbInterface:
					{
						switch (nameIndex)
						{
							case 0: // object_name
								answerCallback.invoke<controller::Interface::SetAvbInterfaceNameHandler>(controllerInterface, targetID, status, configurationIndex, descriptorIndex);
								if (aem.getUnsolicited() && delegate && !!status)
								{
									utils::invokeProtectedMethod(&controller::Delegate::onAvbInterfaceNameChanged, delegate, controllerInterface, targetID, configurationIndex, descriptorIndex, name);
								}
								break;
							default:
								LOG_CONTROLLER_ENTITY_DEBUG(targetID, "Unhandled nameIndex in SET_NAME response for AvbInterface Descriptor: DescriptorType={} DescriptorIndex={} NameIndex={} ConfigurationIndex={} Name={}", utils::to_integral(descriptorType), descriptorIndex, nameIndex, configurationIndex, name.str());
								break;
						}
						break;
					}
					case model::DescriptorType::ClockSource:
					{
						switch (nameIndex)
						{
							case 0: // object_name
								answerCallback.invoke<controller::Interface::SetClockSourceNameHandler>(controllerInterface, targetID, status, configurationIndex, descriptorIndex);
								if (aem.getUnsolicited() && delegate && !!status)
								{
									utils::invokeProtectedMethod(&controller::Delegate::onClockSourceNameChanged, delegate, controllerInterface, targetID, configurationIndex, descriptorIndex, name);
								}
								break;
							default:
								LOG_CONTROLLER_ENTITY_DEBUG(targetID, "Unhandled nameIndex in SET_NAME response for ClockSource Descriptor: DescriptorType={} DescriptorIndex={} NameIndex={} ConfigurationIndex={} Name={}", utils::to_integral(descriptorType), descriptorIndex, nameIndex, configurationIndex, name.str());
								break;
						}
						break;
					}
					case model::DescriptorType::MemoryObject:
					{
						switch (nameIndex)
						{
							case 0: // object_name
								answerCallback.invoke<controller::Interface::SetMemoryObjectNameHandler>(controllerInterface, targetID, status, configurationIndex, descriptorIndex);
								if (aem.getUnsolicited() && delegate && !!status)
								{
									utils::invokeProtectedMethod(&controller::Delegate::onMemoryObjectNameChanged, delegate, controllerInterface, targetID, configurationIndex, descriptorIndex, name);
								}
								break;
							default:
								LOG_CONTROLLER_ENTITY_DEBUG(targetID, "Unhandled nameIndex in SET_NAME response for MemoryObject Descriptor: DescriptorType={} DescriptorIndex={} NameIndex={} ConfigurationIndex={} Name={}", utils::to_integral(descriptorType), descriptorIndex, nameIndex, configurationIndex, name.str());
								break;
						}
						break;
					}
					case model::DescriptorType::AudioCluster:
					{
						switch (nameIndex)
						{
							case 0: // object_name
								answerCallback.invoke<controller::Interface::SetAudioClusterNameHandler>(controllerInterface, targetID, status, configurationIndex, descriptorIndex);
								if (aem.getUnsolicited() && delegate && !!status)
								{
									utils::invokeProtectedMethod(&controller::Delegate::onAudioClusterNameChanged, delegate, controllerInterface, targetID, configurationIndex, descriptorIndex, name);
								}
								break;
							default:
								LOG_CONTROLLER_ENTITY_DEBUG(targetID, "Unhandled nameIndex in SET_NAME response for AudioCluster Descriptor: DescriptorType={} DescriptorIndex={} NameIndex={} ConfigurationIndex={} Name={}", utils::to_integral(descriptorType), descriptorIndex, nameIndex, configurationIndex, name.str());
								break;
						}
						break;
					}
					case model::DescriptorType::ClockDomain:
					{
						switch (nameIndex)
						{
							case 0: // object_name
								answerCallback.invoke<controller::Interface::SetClockDomainNameHandler>(controllerInterface, targetID, status, configurationIndex, descriptorIndex);
								if (aem.getUnsolicited() && delegate && !!status)
								{
									utils::invokeProtectedMethod(&controller::Delegate::onClockDomainNameChanged, delegate, controllerInterface, targetID, configurationIndex, descriptorIndex, name);
								}
								break;
							default:
								LOG_CONTROLLER_ENTITY_DEBUG(targetID, "Unhandled nameIndex in SET_NAME response for ClockDomain Descriptor: DescriptorType={} DescriptorIndex={} NameIndex={} ConfigurationIndex={} Name={}", utils::to_integral(descriptorType), descriptorIndex, nameIndex, configurationIndex, name.str());
								break;
						}
						break;
					}
					default:
						LOG_CONTROLLER_ENTITY_DEBUG(targetID, "Unhandled descriptorType in SET_NAME response: DescriptorType={} DescriptorIndex={} NameIndex={} ConfigurationIndex={} Name={}", utils::to_integral(descriptorType), descriptorIndex, nameIndex, configurationIndex, name.str());
						break;
				}
			}
		},
		// Get Name
		{ protocol::AemCommandType::GetName.getValue(), [](controller::Delegate* const /*delegate*/, Interface const* const controllerInterface, LocalEntity::AemCommandStatus const status, protocol::AemAecpdu const& aem, LocalEntityImpl<>::AnswerCallback const& answerCallback)
			{
	// Deserialize payload
#ifdef __cpp_structured_bindings
				auto const[descriptorType, descriptorIndex, nameIndex, configurationIndex, name] = protocol::aemPayload::deserializeGetNameResponse(aem.getPayload());
#else // !__cpp_structured_bindings
				auto const result = protocol::aemPayload::deserializeGetNameResponse(aem.getPayload());
				model::DescriptorType const descriptorType = std::get<0>(result);
				model::DescriptorIndex const descriptorIndex = std::get<1>(result);
				std::uint16_t const nameIndex = std::get<2>(result);
				model::ConfigurationIndex const configurationIndex = std::get<3>(result);
				model::AvdeccFixedString const name = std::get<4>(result);
#endif // __cpp_structured_bindings

				auto const targetID = aem.getTargetEntityID();

				// Notify handlers
				switch (descriptorType)
				{
					case model::DescriptorType::Entity:
					{
						if (descriptorIndex != 0)
						{
							LOG_CONTROLLER_ENTITY_DEBUG(targetID, "Invalid descriptorIndex in GET_NAME response for Entity Descriptor: DescriptorIndex={}", descriptorIndex);
						}
						if (configurationIndex != 0)
						{
							LOG_CONTROLLER_ENTITY_DEBUG(targetID, "Invalid configurationIndex in GET_NAME response for Entity Descriptor: ConfigurationIndex={}", configurationIndex);
						}
						switch (nameIndex)
						{
							case 0: // entity_name
								answerCallback.invoke<controller::Interface::GetEntityNameHandler>(controllerInterface, targetID, status, name);
								break;
							case 1: // group_name
								answerCallback.invoke<controller::Interface::GetEntityGroupNameHandler>(controllerInterface, targetID, status, name);
								break;
							default:
								LOG_CONTROLLER_ENTITY_DEBUG(targetID, "Unhandled nameIndex in GET_NAME response for Entity Descriptor: DescriptorType={} DescriptorIndex={} NameIndex={} ConfigurationIndex={} Name={}", utils::to_integral(descriptorType), descriptorIndex, nameIndex, configurationIndex, name.str());
								break;
						}
						break;
					}
					case model::DescriptorType::Configuration:
					{
						if (configurationIndex != 0)
						{
							LOG_CONTROLLER_ENTITY_DEBUG(targetID, "Invalid configurationIndex in GET_NAME response for Configuration Descriptor: ConfigurationIndex={}", configurationIndex);
						}
						switch (nameIndex)
						{
							case 0: // object_name
								answerCallback.invoke<controller::Interface::GetConfigurationNameHandler>(controllerInterface, targetID, status, descriptorIndex, name);
								break;
							default:
								LOG_CONTROLLER_ENTITY_DEBUG(targetID, "Unhandled nameIndex in GET_NAME response for Configuration Descriptor: DescriptorType={} DescriptorIndex={} NameIndex={} ConfigurationIndex={} Name={}", utils::to_integral(descriptorType), descriptorIndex, nameIndex, configurationIndex, name.str());
								break;
						}
						break;
					}
					case model::DescriptorType::AudioUnit:
					{
						switch (nameIndex)
						{
							case 0: // object_name
								answerCallback.invoke<controller::Interface::GetAudioUnitNameHandler>(controllerInterface, targetID, status, configurationIndex, descriptorIndex, name);
								break;
							default:
								LOG_CONTROLLER_ENTITY_DEBUG(targetID, "Unhandled nameIndex in GET_NAME response for AudioUnit Descriptor: DescriptorType={} DescriptorIndex={} NameIndex={} ConfigurationIndex={} Name={}", utils::to_integral(descriptorType), descriptorIndex, nameIndex, configurationIndex, name.str());
								break;
						}
						break;
					}
					case model::DescriptorType::StreamInput:
					{
						switch (nameIndex)
						{
							case 0: // object_name
								answerCallback.invoke<controller::Interface::GetStreamInputNameHandler>(controllerInterface, targetID, status, configurationIndex, descriptorIndex, name);
								break;
							default:
								LOG_CONTROLLER_ENTITY_DEBUG(targetID, "Unhandled nameIndex in GET_NAME response for StreamInput Descriptor: DescriptorType={} DescriptorIndex={} NameIndex={} ConfigurationIndex={} Name={}", utils::to_integral(descriptorType), descriptorIndex, nameIndex, configurationIndex, name.str());
								break;
						}
						break;
					}
					case model::DescriptorType::StreamOutput:
					{
						switch (nameIndex)
						{
							case 0: // object_name
								answerCallback.invoke<controller::Interface::GetStreamOutputNameHandler>(controllerInterface, targetID, status, configurationIndex, descriptorIndex, name);
								break;
							default:
								LOG_CONTROLLER_ENTITY_DEBUG(targetID, "Unhandled nameIndex in GET_NAME response for StreamOutput Descriptor: DescriptorType={} DescriptorIndex={} NameIndex={} ConfigurationIndex={} Name={}", utils::to_integral(descriptorType), descriptorIndex, nameIndex, configurationIndex, name.str());
								break;
						}
						break;
					}
					case model::DescriptorType::AvbInterface:
					{
						switch (nameIndex)
						{
							case 0: // object_name
								answerCallback.invoke<controller::Interface::GetAvbInterfaceNameHandler>(controllerInterface, targetID, status, configurationIndex, descriptorIndex, name);
								break;
							default:
								LOG_CONTROLLER_ENTITY_DEBUG(targetID, "Unhandled nameIndex in GET_NAME response for AvbInterface Descriptor: DescriptorType={} DescriptorIndex={} NameIndex={} ConfigurationIndex={} Name={}", utils::to_integral(descriptorType), descriptorIndex, nameIndex, configurationIndex, name.str());
								break;
						}
						break;
					}
					case model::DescriptorType::ClockSource:
					{
						switch (nameIndex)
						{
							case 0: // object_name
								answerCallback.invoke<controller::Interface::GetClockSourceNameHandler>(controllerInterface, targetID, status, configurationIndex, descriptorIndex, name);
								break;
							default:
								LOG_CONTROLLER_ENTITY_DEBUG(targetID, "Unhandled nameIndex in GET_NAME response for ClockSource Descriptor: DescriptorType={} DescriptorIndex={} NameIndex={} ConfigurationIndex={} Name={}", utils::to_integral(descriptorType), descriptorIndex, nameIndex, configurationIndex, name.str());
								break;
						}
						break;
					}
					case model::DescriptorType::MemoryObject:
					{
						switch (nameIndex)
						{
							case 0: // object_name
								answerCallback.invoke<controller::Interface::GetMemoryObjectNameHandler>(controllerInterface, targetID, status, configurationIndex, descriptorIndex, name);
								break;
							default:
								LOG_CONTROLLER_ENTITY_DEBUG(targetID, "Unhandled nameIndex in GET_NAME response for MemoryObject Descriptor: DescriptorType={} DescriptorIndex={} NameIndex={} ConfigurationIndex={} Name={}", utils::to_integral(descriptorType), descriptorIndex, nameIndex, configurationIndex, name.str());
								break;
						}
						break;
					}
					case model::DescriptorType::AudioCluster:
					{
						switch (nameIndex)
						{
							case 0: // object_name
								answerCallback.invoke<controller::Interface::GetAudioClusterNameHandler>(controllerInterface, targetID, status, configurationIndex, descriptorIndex, name);
								break;
							default:
								LOG_CONTROLLER_ENTITY_DEBUG(targetID, "Unhandled nameIndex in GET_NAME response for AudioCluster Descriptor: DescriptorType={} DescriptorIndex={} NameIndex={} ConfigurationIndex={} Name={}", utils::to_integral(descriptorType), descriptorIndex, nameIndex, configurationIndex, name.str());
								break;
						}
						break;
					}
					case model::DescriptorType::ClockDomain:
					{
						switch (nameIndex)
						{
							case 0: // object_name
								answerCallback.invoke<controller::Interface::GetClockDomainNameHandler>(controllerInterface, targetID, status, configurationIndex, descriptorIndex, name);
								break;
							default:
								LOG_CONTROLLER_ENTITY_DEBUG(targetID, "Unhandled nameIndex in GET_NAME response for ClockDomain Descriptor: DescriptorType={} DescriptorIndex={} NameIndex={} ConfigurationIndex={} Name={}", utils::to_integral(descriptorType), descriptorIndex, nameIndex, configurationIndex, name.str());
								break;
						}
						break;
					}
					default:
						LOG_CONTROLLER_ENTITY_DEBUG(targetID, "Unhandled descriptorType in GET_NAME response: DescriptorType={} DescriptorIndex={} NameIndex={} ConfigurationIndex={} Name={}", utils::to_integral(descriptorType), descriptorIndex, nameIndex, configurationIndex, name.str());
						break;
				}
			}
		},
		// Set Sampling Rate
		{ protocol::AemCommandType::SetSamplingRate.getValue(), [](controller::Delegate* const delegate, Interface const* const controllerInterface, LocalEntity::AemCommandStatus const status, protocol::AemAecpdu const& aem, LocalEntityImpl<>::AnswerCallback const& answerCallback)
			{
	// Deserialize payload
#ifdef __cpp_structured_bindings
				auto const[descriptorType, descriptorIndex, samplingRate] = protocol::aemPayload::deserializeSetSamplingRateResponse(aem.getPayload());
#else // !__cpp_structured_bindings
				auto const result = protocol::aemPayload::deserializeSetSamplingRateResponse(aem.getPayload());
				entity::model::DescriptorType const descriptorType = std::get<0>(result);
				entity::model::DescriptorIndex const descriptorIndex = std::get<1>(result);
				entity::model::SamplingRate const samplingRate = std::get<2>(result);
#endif // __cpp_structured_bindings

				auto const targetID = aem.getTargetEntityID();

				// Notify handlers
				if (descriptorType == model::DescriptorType::AudioUnit)
				{
					answerCallback.invoke<controller::Interface::SetAudioUnitSamplingRateHandler>(controllerInterface, targetID, status, descriptorIndex, samplingRate);
					if (aem.getUnsolicited() && delegate && !!status)
					{
						utils::invokeProtectedMethod(&controller::Delegate::onAudioUnitSamplingRateChanged, delegate, controllerInterface, targetID, descriptorIndex, samplingRate);
					}
				}
				else if (descriptorType == model::DescriptorType::VideoCluster)
				{
					answerCallback.invoke<controller::Interface::SetVideoClusterSamplingRateHandler>(controllerInterface, targetID, status, descriptorIndex, samplingRate);
					if (aem.getUnsolicited() && delegate && !!status)
					{
						utils::invokeProtectedMethod(&controller::Delegate::onVideoClusterSamplingRateChanged, delegate, controllerInterface, targetID, descriptorIndex, samplingRate);
					}
				}
				else if (descriptorType == model::DescriptorType::SensorCluster)
				{
					answerCallback.invoke<controller::Interface::SetSensorClusterSamplingRateHandler>(controllerInterface, targetID, status, descriptorIndex, samplingRate);
					if (aem.getUnsolicited() && delegate && !!status)
					{
						utils::invokeProtectedMethod(&controller::Delegate::onSensorClusterSamplingRateChanged, delegate, controllerInterface, targetID, descriptorIndex , samplingRate);
					}
				}
				else
					throw InvalidDescriptorTypeException();
			}
		},
		// Get Sampling Rate
		{ protocol::AemCommandType::GetSamplingRate.getValue(), [](controller::Delegate* const /*delegate*/, Interface const* const controllerInterface, LocalEntity::AemCommandStatus const status, protocol::AemAecpdu const& aem, LocalEntityImpl<>::AnswerCallback const& answerCallback)
			{
	// Deserialize payload
#ifdef __cpp_structured_bindings
				auto const[descriptorType, descriptorIndex, samplingRate] = protocol::aemPayload::deserializeGetSamplingRateResponse(aem.getPayload());
#else // !__cpp_structured_bindings
				auto const result = protocol::aemPayload::deserializeGetSamplingRateResponse(aem.getPayload());
				entity::model::DescriptorType const descriptorType = std::get<0>(result);
				entity::model::DescriptorIndex const descriptorIndex = std::get<1>(result);
				entity::model::SamplingRate const samplingRate = std::get<2>(result);
#endif // __cpp_structured_bindings

				auto const targetID = aem.getTargetEntityID();

				// Notify handlers
				if (descriptorType == model::DescriptorType::AudioUnit)
				{
					answerCallback.invoke<controller::Interface::GetAudioUnitSamplingRateHandler>(controllerInterface, targetID, status, descriptorIndex, samplingRate);
				}
				else if (descriptorType == model::DescriptorType::VideoCluster)
				{
					answerCallback.invoke<controller::Interface::GetVideoClusterSamplingRateHandler>(controllerInterface, targetID, status, descriptorIndex, samplingRate);
				}
				else if (descriptorType == model::DescriptorType::SensorCluster)
				{
					answerCallback.invoke<controller::Interface::GetSensorClusterSamplingRateHandler>(controllerInterface, targetID, status, descriptorIndex, samplingRate);
				}
				else
					throw InvalidDescriptorTypeException();
			}
		},
		// Set Clock Source
		{ protocol::AemCommandType::SetClockSource.getValue(), [](controller::Delegate* const delegate, Interface const* const controllerInterface, LocalEntity::AemCommandStatus const status, protocol::AemAecpdu const& aem, LocalEntityImpl<>::AnswerCallback const& answerCallback)
			{
	// Deserialize payload
#ifdef __cpp_structured_bindings
				auto const[descriptorType, descriptorIndex, clockSourceIndex] = protocol::aemPayload::deserializeSetClockSourceResponse(aem.getPayload());
#else // !__cpp_structured_bindings
				auto const result = protocol::aemPayload::deserializeSetClockSourceResponse(aem.getPayload());
				//entity::model::DescriptorType const descriptorType = std::get<0>(result);
				entity::model::DescriptorIndex const descriptorIndex = std::get<1>(result);
				entity::model::ClockSourceIndex const clockSourceIndex = std::get<2>(result);
#endif // __cpp_structured_bindings

				auto const targetID = aem.getTargetEntityID();

				// Notify handlers
				answerCallback.invoke<controller::Interface::SetClockSourceHandler>(controllerInterface, targetID, status, descriptorIndex, clockSourceIndex);
				if (aem.getUnsolicited() && delegate && !!status)
				{
					utils::invokeProtectedMethod(&controller::Delegate::onClockSourceChanged, delegate, controllerInterface, targetID, descriptorIndex, clockSourceIndex);
				}
			}
		},
		// Get Clock Source
		{ protocol::AemCommandType::GetClockSource.getValue(),[](controller::Delegate* const /*delegate*/, Interface const* const controllerInterface, LocalEntity::AemCommandStatus const status, protocol::AemAecpdu const& aem, LocalEntityImpl<>::AnswerCallback const& answerCallback)
			{
	// Deserialize payload
#ifdef __cpp_structured_bindings
				auto const[descriptorType, descriptorIndex, clockSourceIndex] = protocol::aemPayload::deserializeGetClockSourceResponse(aem.getPayload());
#else // !__cpp_structured_bindings
				auto const result = protocol::aemPayload::deserializeGetClockSourceResponse(aem.getPayload());
				//entity::model::DescriptorType const descriptorType = std::get<0>(result);
				entity::model::DescriptorIndex const descriptorIndex = std::get<1>(result);
				entity::model::ClockSourceIndex const clockSourceIndex = std::get<2>(result);
#endif // __cpp_structured_bindings

				auto const targetID = aem.getTargetEntityID();

				// Notify handlers
				answerCallback.invoke<controller::Interface::GetClockSourceHandler>(controllerInterface, targetID, status, descriptorIndex, clockSourceIndex);
			}
		},
		// Start Streaming
		{ protocol::AemCommandType::StartStreaming.getValue(), [](controller::Delegate* const delegate, Interface const* const controllerInterface, LocalEntity::AemCommandStatus const status, protocol::AemAecpdu const& aem, LocalEntityImpl<>::AnswerCallback const& answerCallback)
			{
	// Deserialize payload
#ifdef __cpp_structured_bindings
				auto const[descriptorType, descriptorIndex] = protocol::aemPayload::deserializeStartStreamingResponse(aem.getPayload());
#else // !__cpp_structured_bindings
				auto const result = protocol::aemPayload::deserializeStartStreamingResponse(aem.getPayload());
				entity::model::DescriptorType const descriptorType = std::get<0>(result);
				entity::model::DescriptorIndex const descriptorIndex = std::get<1>(result);
#endif // __cpp_structured_bindings

				auto const targetID = aem.getTargetEntityID();

				// Notify handlers
				if (descriptorType == model::DescriptorType::StreamInput)
				{
					answerCallback.invoke<controller::Interface::StartStreamInputHandler>(controllerInterface, targetID, status, descriptorIndex);
					if (aem.getUnsolicited() && delegate && !!status)
					{
						utils::invokeProtectedMethod(&controller::Delegate::onStreamInputStarted, delegate, controllerInterface, targetID, descriptorIndex);
					}
				}
				else if (descriptorType == model::DescriptorType::StreamOutput)
				{
					answerCallback.invoke<controller::Interface::StartStreamOutputHandler>(controllerInterface, targetID, status, descriptorIndex);
					if (aem.getUnsolicited() && delegate && !!status)
					{
						utils::invokeProtectedMethod(&controller::Delegate::onStreamOutputStarted, delegate, controllerInterface, targetID, descriptorIndex);
					}
				}
				else
					throw InvalidDescriptorTypeException();
			}
		},
		// Stop Streaming
		{ protocol::AemCommandType::StopStreaming.getValue(), [](controller::Delegate* const delegate, Interface const* const controllerInterface, LocalEntity::AemCommandStatus const status, protocol::AemAecpdu const& aem, LocalEntityImpl<>::AnswerCallback const& answerCallback)
			{
	// Deserialize payload
#ifdef __cpp_structured_bindings
				auto const[descriptorType, descriptorIndex] = protocol::aemPayload::deserializeStopStreamingResponse(aem.getPayload());
#else // !__cpp_structured_bindings
				auto const result = protocol::aemPayload::deserializeStopStreamingResponse(aem.getPayload());
				entity::model::DescriptorType const descriptorType = std::get<0>(result);
				entity::model::DescriptorIndex const descriptorIndex = std::get<1>(result);
#endif // __cpp_structured_bindings

				auto const targetID = aem.getTargetEntityID();

				// Notify handlers
				if (descriptorType == model::DescriptorType::StreamInput)
				{
					answerCallback.invoke<controller::Interface::StopStreamInputHandler>(controllerInterface, targetID, status, descriptorIndex);
					if (aem.getUnsolicited() && delegate && !!status)
					{
						utils::invokeProtectedMethod(&controller::Delegate::onStreamInputStopped, delegate, controllerInterface, targetID, descriptorIndex);
					}
				}
				else if (descriptorType == model::DescriptorType::StreamOutput)
				{
					answerCallback.invoke<controller::Interface::StopStreamOutputHandler>(controllerInterface, targetID, status, descriptorIndex);
					if (aem.getUnsolicited() && delegate && !!status)
					{
						utils::invokeProtectedMethod(&controller::Delegate::onStreamOutputStopped, delegate, controllerInterface, targetID, descriptorIndex);
					}
				}
				else
					throw InvalidDescriptorTypeException();
			}
		},
		// Register Unsolicited Notifications
		{ protocol::AemCommandType::RegisterUnsolicitedNotification.getValue(), [](controller::Delegate* const /*delegate*/, Interface const* const controllerInterface, LocalEntity::AemCommandStatus const status, protocol::AemAecpdu const& aem, LocalEntityImpl<>::AnswerCallback const& answerCallback)
			{
				// Ignore payload size and content, Apple's implementation is bugged and returns too much data
				auto const targetID = aem.getTargetEntityID();
				answerCallback.invoke<controller::Interface::RegisterUnsolicitedNotificationsHandler>(controllerInterface, targetID, status);
			}
		},
		// Unregister Unsolicited Notifications
		{ protocol::AemCommandType::DeregisterUnsolicitedNotification.getValue(), [](controller::Delegate* const delegate, Interface const* const controllerInterface, LocalEntity::AemCommandStatus const status, protocol::AemAecpdu const& aem, LocalEntityImpl<>::AnswerCallback const& answerCallback)
			{
				// Ignore payload size and content, Apple's implementation is bugged and returns too much data
				auto const targetID = aem.getTargetEntityID();

				answerCallback.invoke<controller::Interface::UnregisterUnsolicitedNotificationsHandler>(controllerInterface, targetID, status);
				if (aem.getUnsolicited() && delegate && !!status)
				{
					utils::invokeProtectedMethod(&controller::Delegate::onDeregisteredFromUnsolicitedNotifications, delegate, controllerInterface, targetID);
				}
			}
		},
		// GetAvbInfo
		{ protocol::AemCommandType::GetAvbInfo.getValue(), [](controller::Delegate* const delegate, Interface const* const controllerInterface, LocalEntity::AemCommandStatus const status, protocol::AemAecpdu const& aem, LocalEntityImpl<>::AnswerCallback const& answerCallback)
			{
	// Deserialize payload
#ifdef __cpp_structured_bindings
				auto const[descriptorType, descriptorIndex, avbInfo] = protocol::aemPayload::deserializeGetAvbInfoResponse(aem.getPayload());
#else // !__cpp_structured_bindings
				auto const result = protocol::aemPayload::deserializeGetAvbInfoResponse(aem.getPayload());
				entity::model::DescriptorType const descriptorType = std::get<0>(result);
				entity::model::DescriptorIndex const descriptorIndex = std::get<1>(result);
				entity::model::AvbInfo const avbInfo = std::get<2>(result);
#endif // __cpp_structured_bindings

				auto const targetID = aem.getTargetEntityID();

				// Notify handlers
				if (descriptorType == model::DescriptorType::AvbInterface)
				{
					answerCallback.invoke<controller::Interface::GetAvbInfoHandler>(controllerInterface, targetID, status, descriptorIndex, avbInfo);
					if (aem.getUnsolicited() && delegate && !!status) // Unsolicited triggered by change in the SRP domain (Clause 7.5.2)
					{
						utils::invokeProtectedMethod(&controller::Delegate::onAvbInfoChanged, delegate, controllerInterface, targetID, descriptorIndex, avbInfo);
					}
				}
				else
					throw InvalidDescriptorTypeException();
			}
		},
		// GetAsPath
		{ protocol::AemCommandType::GetAsPath.getValue(), [](controller::Delegate* const delegate, Interface const* const controllerInterface, LocalEntity::AemCommandStatus const status, protocol::AemAecpdu const& aem, LocalEntityImpl<>::AnswerCallback const& answerCallback)
			{
	// Deserialize payload
#ifdef __cpp_structured_bindings
				auto const[descriptorIndex, asPath] = protocol::aemPayload::deserializeGetAsPathResponse(aem.getPayload());
#else // !__cpp_structured_bindings
				auto const result = protocol::aemPayload::deserializeGetAsPathResponse(aem.getPayload());
				entity::model::DescriptorIndex const descriptorIndex = std::get<0>(result);
				entity::model::AsPath const asPath = std::get<1>(result);
#endif // __cpp_structured_bindings

				auto const targetID = aem.getTargetEntityID();

				// Notify handlers
				answerCallback.invoke<controller::Interface::GetAsPathHandler>(controllerInterface, targetID, status, descriptorIndex, asPath);
				if (aem.getUnsolicited() && delegate && !!status) // Unsolicited triggered by change in the SRP domain (Clause 7.5.2)
				{
					utils::invokeProtectedMethod(&controller::Delegate::onAsPathChanged, delegate, controllerInterface, targetID, descriptorIndex, asPath);
				}
			}
		},
		// GetCounters
		{ protocol::AemCommandType::GetCounters.getValue(), [](controller::Delegate* const delegate, Interface const* const controllerInterface, LocalEntity::AemCommandStatus const status, protocol::AemAecpdu const& aem, LocalEntityImpl<>::AnswerCallback const& answerCallback)
			{
	// Deserialize payload
#ifdef __cpp_structured_bindings
				auto const[descriptorType, descriptorIndex, validFlags, counters] = protocol::aemPayload::deserializeGetCountersResponse(aem.getPayload());
#else // !__cpp_structured_bindings
				auto const result = protocol::aemPayload::deserializeGetCountersResponse(aem.getPayload());
				entity::model::DescriptorType const descriptorType = std::get<0>(result);
				entity::model::DescriptorIndex const descriptorIndex = std::get<1>(result);
				entity::model::DescriptorCounterValidFlag const validFlags = std::get<2>(result);
				entity::model::DescriptorCounters const& counters = std::get<3>(result);
#endif // __cpp_structured_bindings

				auto const targetID = aem.getTargetEntityID();

				// Notify handlers
				switch (descriptorType)
				{
					case model::DescriptorType::AvbInterface:
					{
						AvbInterfaceCounterValidFlags flags;
						flags.assign(validFlags);
						answerCallback.invoke<controller::Interface::GetAvbInterfaceCountersHandler>(controllerInterface, targetID, status, descriptorIndex, flags, counters);
						if (aem.getUnsolicited() && delegate && !!status)
						{
							utils::invokeProtectedMethod(&controller::Delegate::onAvbInterfaceCountersChanged, delegate, controllerInterface, targetID, descriptorIndex, flags, counters);
						}
						break;
					}
					case model::DescriptorType::ClockDomain:
					{
						ClockDomainCounterValidFlags flags;
						flags.assign(validFlags);
						answerCallback.invoke<controller::Interface::GetClockDomainCountersHandler>(controllerInterface, targetID, status, descriptorIndex, flags, counters);
						if (aem.getUnsolicited() && delegate && !!status)
						{
							utils::invokeProtectedMethod(&controller::Delegate::onClockDomainCountersChanged, delegate, controllerInterface, targetID, descriptorIndex, flags, counters);
						}
						break;
					}
					case model::DescriptorType::StreamInput:
					{
						StreamInputCounterValidFlags flags;
						flags.assign(validFlags);
						answerCallback.invoke<controller::Interface::GetStreamInputCountersHandler>(controllerInterface, targetID, status, descriptorIndex, flags, counters);
						if (aem.getUnsolicited() && delegate && !!status)
						{
							utils::invokeProtectedMethod(&controller::Delegate::onStreamInputCountersChanged, delegate, controllerInterface, targetID, descriptorIndex, flags, counters);
						}
						break;
					}
					case model::DescriptorType::StreamOutput:
					{
						StreamOutputCounterValidFlags flags;
						flags.assign(validFlags);
						answerCallback.invoke<controller::Interface::GetStreamOutputCountersHandler>(controllerInterface, targetID, status, descriptorIndex, flags, counters);
						if (aem.getUnsolicited() && delegate && !!status)
						{
							utils::invokeProtectedMethod(&controller::Delegate::onStreamOutputCountersChanged, delegate, controllerInterface, targetID, descriptorIndex, flags, counters);
						}
						break;
					}
					default:
						LOG_CONTROLLER_ENTITY_DEBUG(targetID, "Unhandled descriptorType in GET_COUNTERS response: DescriptorType={} DescriptorIndex={}", utils::to_integral(descriptorType), descriptorIndex);
						break;
				}
			}
		},
		// Get Audio Map
		{ protocol::AemCommandType::GetAudioMap.getValue(), [](controller::Delegate* const delegate, Interface const* const controllerInterface, LocalEntity::AemCommandStatus const status, protocol::AemAecpdu const& aem, LocalEntityImpl<>::AnswerCallback const& answerCallback)
			{
	// Deserialize payload
#ifdef __cpp_structured_bindings
				auto const[descriptorType, descriptorIndex, mapIndex, numberOfMaps, mappings] = protocol::aemPayload::deserializeGetAudioMapResponse(aem.getPayload());
#else // !__cpp_structured_bindings
				auto const result = protocol::aemPayload::deserializeGetAudioMapResponse(aem.getPayload());
				entity::model::DescriptorType const descriptorType = std::get<0>(result);
				entity::model::DescriptorIndex const descriptorIndex = std::get<1>(result);
				entity::model::MapIndex const mapIndex = std::get<2>(result);
				entity::model::MapIndex const numberOfMaps = std::get<3>(result);
				entity::model::AudioMappings const& mappings = std::get<4>(result);
#endif // __cpp_structured_bindings

				auto const targetID = aem.getTargetEntityID();

				// Notify handlers
				if (descriptorType == model::DescriptorType::StreamPortInput)
				{
					answerCallback.invoke<controller::Interface::GetStreamPortInputAudioMapHandler>(controllerInterface, targetID, status, descriptorIndex, numberOfMaps, mapIndex, mappings);
					if (aem.getUnsolicited() && delegate && !!status)
					{
						utils::invokeProtectedMethod(&controller::Delegate::onStreamPortInputAudioMappingsChanged, delegate, controllerInterface, targetID, descriptorIndex, numberOfMaps, mapIndex, mappings);
					}
				}
				else if (descriptorType == model::DescriptorType::StreamPortOutput)
				{
					answerCallback.invoke<controller::Interface::GetStreamPortOutputAudioMapHandler>(controllerInterface, targetID, status, descriptorIndex, numberOfMaps, mapIndex, mappings);
					if (aem.getUnsolicited() && delegate && !!status)
					{
						utils::invokeProtectedMethod(&controller::Delegate::onStreamPortOutputAudioMappingsChanged, delegate, controllerInterface, targetID, descriptorIndex, numberOfMaps, mapIndex, mappings);
					}
				}
				else
					throw InvalidDescriptorTypeException();
			}
		},
		// Add Audio Mappings
		{ protocol::AemCommandType::AddAudioMappings.getValue(), [](controller::Delegate* const delegate, Interface const* const controllerInterface, LocalEntity::AemCommandStatus const status, protocol::AemAecpdu const& aem, LocalEntityImpl<>::AnswerCallback const& answerCallback)
			{
	// Deserialize payload
#ifdef __cpp_structured_bindings
				auto const[descriptorType, descriptorIndex, mappings] = protocol::aemPayload::deserializeAddAudioMappingsResponse(aem.getPayload());
#else // !__cpp_structured_bindings
				auto const result = protocol::aemPayload::deserializeAddAudioMappingsResponse(aem.getPayload());
				entity::model::DescriptorType const descriptorType = std::get<0>(result);
				entity::model::DescriptorIndex const descriptorIndex = std::get<1>(result);
				entity::model::AudioMappings const& mappings = std::get<2>(result);
#endif // __cpp_structured_bindings

				auto const targetID = aem.getTargetEntityID();

		// Notify handlers
				if (descriptorType == model::DescriptorType::StreamPortInput)
				{
					answerCallback.invoke<controller::Interface::AddStreamPortInputAudioMappingsHandler>(controllerInterface, targetID, status, descriptorIndex, mappings);
					if (aem.getUnsolicited() && delegate && !!status)
					{
						utils::invokeProtectedMethod(&controller::Delegate::onStreamPortInputAudioMappingsAdded, delegate, controllerInterface, targetID, descriptorIndex, mappings);
					}
				}
				else if (descriptorType == model::DescriptorType::StreamPortOutput)
				{
					answerCallback.invoke<controller::Interface::AddStreamPortOutputAudioMappingsHandler>(controllerInterface, targetID, status, descriptorIndex, mappings);
					if (aem.getUnsolicited() && delegate && !!status)
					{
						utils::invokeProtectedMethod(&controller::Delegate::onStreamPortOutputAudioMappingsAdded, delegate, controllerInterface, targetID, descriptorIndex, mappings);
					}
				}
				else
					throw InvalidDescriptorTypeException();
			}
		},
		// Remove Audio Mappings
		{ protocol::AemCommandType::RemoveAudioMappings.getValue(), [](controller::Delegate* const delegate, Interface const* const controllerInterface, LocalEntity::AemCommandStatus const status, protocol::AemAecpdu const& aem, LocalEntityImpl<>::AnswerCallback const& answerCallback)
			{
	// Deserialize payload
#ifdef __cpp_structured_bindings
				auto const[descriptorType, descriptorIndex, mappings] = protocol::aemPayload::deserializeRemoveAudioMappingsResponse(aem.getPayload());
#else // !__cpp_structured_bindings
				auto const result = protocol::aemPayload::deserializeRemoveAudioMappingsResponse(aem.getPayload());
				entity::model::DescriptorType const descriptorType = std::get<0>(result);
				entity::model::DescriptorIndex const descriptorIndex = std::get<1>(result);
				entity::model::AudioMappings const& mappings = std::get<2>(result);
#endif // __cpp_structured_bindings

				auto const targetID = aem.getTargetEntityID();

		// Notify handlers
				if (descriptorType == model::DescriptorType::StreamPortInput)
				{
					answerCallback.invoke<controller::Interface::RemoveStreamPortInputAudioMappingsHandler>(controllerInterface, targetID, status, descriptorIndex, mappings);
					if (aem.getUnsolicited() && delegate && !!status)
					{
						utils::invokeProtectedMethod(&controller::Delegate::onStreamPortInputAudioMappingsRemoved, delegate, controllerInterface, targetID, descriptorIndex, mappings);
					}
				}
				else if (descriptorType == model::DescriptorType::StreamPortOutput)
				{
					answerCallback.invoke<controller::Interface::RemoveStreamPortOutputAudioMappingsHandler>(controllerInterface, targetID, status, descriptorIndex, mappings);
					if (aem.getUnsolicited() && delegate && !!status)
					{
						utils::invokeProtectedMethod(&controller::Delegate::onStreamPortOutputAudioMappingsRemoved, delegate, controllerInterface, targetID, descriptorIndex, mappings);
					}
				}
				else
					throw InvalidDescriptorTypeException();
			}
		},
		// Start Operation
		{ protocol::AemCommandType::StartOperation.getValue(), [](controller::Delegate* const /*delegate*/, Interface const* const controllerInterface, LocalEntity::AemCommandStatus const status, protocol::AemAecpdu const& aem, LocalEntityImpl<>::AnswerCallback const& answerCallback)
			{
	// Deserialize payload
#ifdef __cpp_structured_bindings
				auto const[descriptorType, descriptorIndex, operationID, operationType, memoryBuffer] = protocol::aemPayload::deserializeStartOperationResponse(aem.getPayload());
#else // !__cpp_structured_bindings
				auto const result = protocol::aemPayload::deserializeStartOperationResponse(aem.getPayload());
				entity::model::DescriptorType const descriptorType = std::get<0>(result);
				entity::model::DescriptorIndex const descriptorIndex = std::get<1>(result);
				entity::model::OperationID const operationID = std::get<2>(result);
				entity::model::MemoryObjectOperationType const operationType = std::get<3>(result);
				MemoryBuffer const memoryBuffer = std::get<4>(result);
#endif // __cpp_structured_bindings

				auto const targetID = aem.getTargetEntityID();

				// Notify handlers
				answerCallback.invoke<controller::Interface::StartOperationHandler>(controllerInterface, targetID, status, descriptorType, descriptorIndex, operationID, operationType, memoryBuffer);
			}
		},
		// Abort Operation
		{ protocol::AemCommandType::AbortOperation.getValue(), [](controller::Delegate* const /*delegate*/, Interface const* const controllerInterface, LocalEntity::AemCommandStatus const status, protocol::AemAecpdu const& aem, LocalEntityImpl<>::AnswerCallback const& answerCallback)
			{
	// Deserialize payload
#ifdef __cpp_structured_bindings
				auto const[descriptorType, descriptorIndex, operationID] = protocol::aemPayload::deserializeAbortOperationResponse(aem.getPayload());
#else // !__cpp_structured_bindings
				auto const result = protocol::aemPayload::deserializeAbortOperationResponse(aem.getPayload());
				entity::model::DescriptorType const descriptorType = std::get<0>(result);
				entity::model::DescriptorIndex const descriptorIndex = std::get<1>(result);
				entity::model::OperationID const operationID = std::get<2>(result);
#endif // __cpp_structured_bindings

				auto const targetID = aem.getTargetEntityID();

				// Notify handlers
				answerCallback.invoke<controller::Interface::AbortOperationHandler>(controllerInterface, targetID, status, descriptorType, descriptorIndex, operationID);
			}
		},
		// Operation Status
		{ protocol::AemCommandType::OperationStatus.getValue(), [](controller::Delegate* const delegate, Interface const* const controllerInterface, LocalEntity::AemCommandStatus const /*status*/, protocol::AemAecpdu const& aem, LocalEntityImpl<>::AnswerCallback const& /*answerCallback*/)
			{
	// Deserialize payload
#ifdef __cpp_structured_bindings
				auto const[descriptorType, descriptorIndex, operationID, percentComplete] = protocol::aemPayload::deserializeOperationStatusResponse(aem.getPayload());
#else // !__cpp_structured_bindings
				auto const result = protocol::aemPayload::deserializeOperationStatusResponse(aem.getPayload());
				entity::model::DescriptorType const descriptorType = std::get<0>(result);
				entity::model::DescriptorIndex const descriptorIndex = std::get<1>(result);
				entity::model::OperationID const operationID = std::get<2>(result);
				std::uint16_t const percentComplete = std::get<3>(result);
#endif // __cpp_structured_bindings

				auto const targetID = aem.getTargetEntityID();

				// Notify handlers
				AVDECC_ASSERT(aem.getUnsolicited(), "OperationStatus can only be an unsolicited response");
				utils::invokeProtectedMethod(&controller::Delegate::onOperationStatus, delegate, controllerInterface, targetID, descriptorType, descriptorIndex, operationID, percentComplete);
			}
		},
		// Set Memory Object Length
		{ protocol::AemCommandType::SetMemoryObjectLength.getValue(), [](controller::Delegate* const delegate, Interface const* const controllerInterface, LocalEntity::AemCommandStatus const status, protocol::AemAecpdu const& aem, LocalEntityImpl<>::AnswerCallback const& answerCallback)
			{
	// Deserialize payload
#ifdef __cpp_structured_bindings
				auto const[configurationIndex, memoryObjectIndex, length] = protocol::aemPayload::deserializeSetMemoryObjectLengthResponse(aem.getPayload());
#else // !__cpp_structured_bindings
				auto const result = protocol::aemPayload::deserializeSetMemoryObjectLengthResponse(aem.getPayload());
				entity::model::ConfigurationIndex const configurationIndex = std::get<0>(result);
				entity::model::MemoryObjectIndex const memoryObjectIndex = std::get<1>(result);
				std::uint64_t const length = std::get<2>(result);
#endif // __cpp_structured_bindings

				auto const targetID = aem.getTargetEntityID();

				// Notify handlers
				answerCallback.invoke<controller::Interface::SetMemoryObjectLengthHandler>(controllerInterface, targetID, status, configurationIndex, memoryObjectIndex, length);
				if (aem.getUnsolicited() && delegate && !!status)
				{
					utils::invokeProtectedMethod(&controller::Delegate::onMemoryObjectLengthChanged, delegate, controllerInterface, targetID, configurationIndex, memoryObjectIndex, length);
				}
			}
		},
		// Get Memory Object Length
		{ protocol::AemCommandType::GetMemoryObjectLength.getValue(),[](controller::Delegate* const /*delegate*/, Interface const* const controllerInterface, LocalEntity::AemCommandStatus const status, protocol::AemAecpdu const& aem, LocalEntityImpl<>::AnswerCallback const& answerCallback)
			{
	// Deserialize payload
#ifdef __cpp_structured_bindings
				auto const[configurationIndex, memoryObjectIndex, length] = protocol::aemPayload::deserializeGetMemoryObjectLengthResponse(aem.getPayload());
#else // !__cpp_structured_bindings
				auto const result = protocol::aemPayload::deserializeGetMemoryObjectLengthResponse(aem.getPayload());
				entity::model::ConfigurationIndex const configurationIndex = std::get<0>(result);
				entity::model::MemoryObjectIndex const memoryObjectIndex = std::get<1>(result);
				std::uint64_t const length = std::get<2>(result);
#endif // __cpp_structured_bindings

				auto const targetID = aem.getTargetEntityID();

				// Notify handlers
				answerCallback.invoke<controller::Interface::GetMemoryObjectLengthHandler>(controllerInterface, targetID, status, configurationIndex, memoryObjectIndex, length);
			}
		},
		// Set Stream Backup
		// Get Stream Backup
	};

	auto const& it = s_Dispatch.find(aem.getCommandType().getValue());
	if (it == s_Dispatch.end())
	{
		// If this is an unsolicited notification, simply log we do not handle the message
		if (aem.getUnsolicited())
		{
			LOG_CONTROLLER_ENTITY_DEBUG(aem.getTargetEntityID(), "Unsolicited AEM response {} not handled ({})", std::string(aem.getCommandType()), utils::toHexString(aem.getCommandType().getValue()));
		}
		// But if it's an expected response, this is an internal error since we sent a command and didn't implement the code to handle the response
		else
		{
			LOG_CONTROLLER_ENTITY_ERROR(aem.getTargetEntityID(), "Failed to process AEM response: Unhandled command type {} ({})", std::string(aem.getCommandType()), utils::toHexString(aem.getCommandType().getValue()));
			utils::invokeProtectedHandler(onErrorCallback, LocalEntity::AemCommandStatus::InternalError);
		}
	}
	else
	{
		auto checkProcessInvalidNonSuccessResponse = [status, &aem, &onErrorCallback]([[maybe_unused]] char const* const what)
		{
			auto st = LocalEntity::AemCommandStatus::ProtocolError;
#if defined(IGNORE_INVALID_NON_SUCCESS_AEM_RESPONSES)
			if (status != LocalEntity::AemCommandStatus::Success)
			{
				// Allow this packet to go through as a non-success response, but some fields might have the default initial value which might not be valid (the spec says even in a response message, some fields have a meaningful value)
				st = status;
				LOG_CONTROLLER_ENTITY_INFO(aem.getTargetEntityID(), "Received an invalid non-success {} AEM response ({}) from {} but still processing it because of compilation option IGNORE_INVALID_NON_SUCCESS_AEM_RESPONSES", std::string(aem.getCommandType()), what, utils::toHexString(aem.getTargetEntityID(), true));
			}
#endif // IGNORE_INVALID_NON_SUCCESS_AEM_RESPONSES
			if (st == LocalEntity::AemCommandStatus::ProtocolError)
			{
				LOG_CONTROLLER_ENTITY_ERROR(aem.getTargetEntityID(), "Failed to process {} AEM response: {}", std::string(aem.getCommandType()), what);
			}
			utils::invokeProtectedHandler(onErrorCallback, st);
		};

		try
		{
			it->second(_controllerDelegate, &_controllerInterface, status, aem, answerCallback);
		}
		catch (protocol::aemPayload::IncorrectPayloadSizeException const& e)
		{
			checkProcessInvalidNonSuccessResponse(e.what());
			return;
		}
		catch (InvalidDescriptorTypeException const& e)
		{
			checkProcessInvalidNonSuccessResponse(e.what());
			return;
		}
		catch ([[maybe_unused]] std::exception const& e) // Mainly unpacking errors
		{
			LOG_CONTROLLER_ENTITY_ERROR(aem.getTargetEntityID(), "Failed to process {} AEM response: {}", std::string(aem.getCommandType()), e.what());
			utils::invokeProtectedHandler(onErrorCallback, LocalEntity::AemCommandStatus::ProtocolError);
			return;
		}
	}
}

void CapabilityDelegate::processAaAecpResponse(protocol::Aecpdu const* const response, LocalEntityImpl<>::OnAaAECPErrorCallback const& /*onErrorCallback*/, LocalEntityImpl<>::AnswerCallback const& answerCallback) const noexcept
{
	auto const& aa = static_cast<protocol::AaAecpdu const&>(*response);
	auto const status = static_cast<LocalEntity::AaCommandStatus>(aa.getStatus().getValue()); // We have to convert protocol status to our extended status
	auto const targetID = aa.getTargetEntityID();

	answerCallback.invoke<controller::Interface::AddressAccessHandler>(&_controllerInterface, targetID, status, aa.getTlvData());
}

void CapabilityDelegate::processMvuAecpResponse(protocol::Aecpdu const* const response, LocalEntityImpl<>::OnMvuAECPErrorCallback const& onErrorCallback, LocalEntityImpl<>::AnswerCallback const& answerCallback) const noexcept
{
	auto const& mvu = static_cast<protocol::MvuAecpdu const&>(*response);
	auto const status = static_cast<LocalEntity::MvuCommandStatus>(mvu.getStatus().getValue()); // We have to convert protocol status to our extended status

	static std::unordered_map<protocol::MvuCommandType::value_type, std::function<void(controller::Delegate* const delegate, Interface const* const controllerInterface, LocalEntity::MvuCommandStatus const status, protocol::MvuAecpdu const& mvu, LocalEntityImpl<>::AnswerCallback const& answerCallback)>> s_Dispatch{
		// Get Milan Info
		{ protocol::MvuCommandType::GetMilanInfo.getValue(),
			[](controller::Delegate* const /*delegate*/, Interface const* const controllerInterface, LocalEntity::MvuCommandStatus const status, protocol::MvuAecpdu const& mvu, LocalEntityImpl<>::AnswerCallback const& answerCallback)
			{
	// Deserialize payload
#ifdef __cpp_structured_bindings
				auto const [milanInfo] = protocol::mvuPayload::deserializeGetMilanInfoResponse(mvu.getPayload());
#else // !__cpp_structured_bindings
				auto const result = protocol::mvuPayload::deserializeGetMilanInfoResponse(mvu.getPayload());
				entity::model::MilanInfo const milanInfo = std::get<0>(result);
#endif // __cpp_structured_bindings

				auto const targetID = mvu.getTargetEntityID();

				answerCallback.invoke<controller::Interface::GetMilanInfoHandler>(controllerInterface, targetID, status, milanInfo);
			} },
	};

	auto const& it = s_Dispatch.find(mvu.getCommandType().getValue());
	if (it == s_Dispatch.end())
	{
		// It's an expected response, this is an internal error since we sent a command and didn't implement the code to handle the response
		LOG_CONTROLLER_ENTITY_ERROR(mvu.getTargetEntityID(), "Failed to process MVU response: Unhandled command type {} ({})", std::string(mvu.getCommandType()), utils::toHexString(mvu.getCommandType().getValue()));
		utils::invokeProtectedHandler(onErrorCallback, LocalEntity::MvuCommandStatus::InternalError);
	}
	else
	{
		try
		{
			it->second(_controllerDelegate, &_controllerInterface, status, mvu, answerCallback);
		}
		catch ([[maybe_unused]] protocol::mvuPayload::IncorrectPayloadSizeException const& e)
		{
			LOG_CONTROLLER_ENTITY_ERROR(mvu.getTargetEntityID(), "Failed to process {} MVU response: {}", std::string(mvu.getCommandType()), e.what());
			utils::invokeProtectedHandler(onErrorCallback, LocalEntity::MvuCommandStatus::ProtocolError);
			return;
		}
		catch ([[maybe_unused]] InvalidDescriptorTypeException const& e)
		{
			LOG_CONTROLLER_ENTITY_ERROR(mvu.getTargetEntityID(), "Failed to process {} MVU response: {}", std::string(mvu.getCommandType()), e.what());
			utils::invokeProtectedHandler(onErrorCallback, LocalEntity::MvuCommandStatus::ProtocolError);
			return;
		}
		catch ([[maybe_unused]] std::exception const& e) // Mainly unpacking errors
		{
			LOG_CONTROLLER_ENTITY_ERROR(mvu.getTargetEntityID(), "Failed to process {} MVU response: {}", std::string(mvu.getCommandType()), e.what());
			utils::invokeProtectedHandler(onErrorCallback, LocalEntity::MvuCommandStatus::ProtocolError);
			return;
		}
	}
}

void CapabilityDelegate::processAcmpResponse(protocol::Acmpdu const* const response, LocalEntityImpl<>::OnACMPErrorCallback const& onErrorCallback, LocalEntityImpl<>::AnswerCallback const& answerCallback, bool const sniffed) const noexcept
{
	auto const& acmp = static_cast<protocol::Acmpdu const&>(*response);
	auto const status = static_cast<LocalEntity::ControlStatus>(acmp.getStatus().getValue()); // We have to convert protocol status to our extended status

	static std::unordered_map<protocol::AcmpMessageType::value_type, std::function<void(controller::Delegate* const delegate, Interface const* const controllerInterface, LocalEntity::ControlStatus const status, protocol::Acmpdu const& acmp, LocalEntityImpl<>::AnswerCallback const& answerCallback, bool const sniffed)>> s_Dispatch{
		// Connect TX response
		{ protocol::AcmpMessageType::ConnectTxResponse.getValue(),
			[](controller::Delegate* const delegate, Interface const* const controllerInterface, LocalEntity::ControlStatus const status, protocol::Acmpdu const& acmp, LocalEntityImpl<>::AnswerCallback const& /*answerCallback*/, bool const sniffed)
			{
				auto const talkerEntityID = acmp.getTalkerEntityID();
				auto const talkerStreamIndex = acmp.getTalkerUniqueID();
				auto const listenerEntityID = acmp.getListenerEntityID();
				auto const listenerStreamIndex = acmp.getListenerUniqueID();
				auto const connectionCount = acmp.getConnectionCount();
				auto const flags = acmp.getFlags();
				if (sniffed && delegate)
				{
					utils::invokeProtectedMethod(&controller::Delegate::onListenerConnectResponseSniffed, delegate, controllerInterface, model::StreamIdentification{ talkerEntityID, talkerStreamIndex }, model::StreamIdentification{ listenerEntityID, listenerStreamIndex }, connectionCount, flags, status);
				}
			} },
		// Disconnect TX response
		{ protocol::AcmpMessageType::DisconnectTxResponse.getValue(),
			[](controller::Delegate* const delegate, Interface const* const controllerInterface, LocalEntity::ControlStatus const status, protocol::Acmpdu const& acmp, LocalEntityImpl<>::AnswerCallback const& answerCallback, bool const sniffed)
			{
				auto const talkerEntityID = acmp.getTalkerEntityID();
				auto const talkerStreamIndex = acmp.getTalkerUniqueID();
				auto const listenerEntityID = acmp.getListenerEntityID();
				auto const listenerStreamIndex = acmp.getListenerUniqueID();
				auto const connectionCount = acmp.getConnectionCount();
				auto const flags = acmp.getFlags();
				answerCallback.invoke<controller::Interface::DisconnectTalkerStreamHandler>(controllerInterface, model::StreamIdentification{ talkerEntityID, talkerStreamIndex }, model::StreamIdentification{ listenerEntityID, listenerStreamIndex }, connectionCount, flags, status);
				if (sniffed && delegate)
				{
					utils::invokeProtectedMethod(&controller::Delegate::onListenerDisconnectResponseSniffed, delegate, controllerInterface, model::StreamIdentification{ talkerEntityID, talkerStreamIndex }, model::StreamIdentification{ listenerEntityID, listenerStreamIndex }, connectionCount, flags, status);
				}
			} },
		// Get TX state response
		{ protocol::AcmpMessageType::GetTxStateResponse.getValue(),
			[](controller::Delegate* const delegate, Interface const* const controllerInterface, LocalEntity::ControlStatus const status, protocol::Acmpdu const& acmp, LocalEntityImpl<>::AnswerCallback const& answerCallback, bool const sniffed)
			{
				auto const talkerEntityID = acmp.getTalkerEntityID();
				auto const talkerStreamIndex = acmp.getTalkerUniqueID();
				auto const listenerEntityID = acmp.getListenerEntityID();
				auto const listenerStreamIndex = acmp.getListenerUniqueID();
				auto const connectionCount = acmp.getConnectionCount();
				auto const flags = acmp.getFlags();
				answerCallback.invoke<controller::Interface::GetTalkerStreamStateHandler>(controllerInterface, model::StreamIdentification{ talkerEntityID, talkerStreamIndex }, model::StreamIdentification{ listenerEntityID, listenerStreamIndex }, connectionCount, flags, status);
				if (sniffed && delegate)
				{
					utils::invokeProtectedMethod(&controller::Delegate::onGetTalkerStreamStateResponseSniffed, delegate, controllerInterface, model::StreamIdentification{ talkerEntityID, talkerStreamIndex }, model::StreamIdentification{ listenerEntityID, listenerStreamIndex }, connectionCount, flags, status);
				}
			} },
		// Connect RX response
		{ protocol::AcmpMessageType::ConnectRxResponse.getValue(),
			[](controller::Delegate* const delegate, Interface const* const controllerInterface, LocalEntity::ControlStatus const status, protocol::Acmpdu const& acmp, LocalEntityImpl<>::AnswerCallback const& answerCallback, bool const sniffed)
			{
				auto const talkerEntityID = acmp.getTalkerEntityID();
				auto const talkerStreamIndex = acmp.getTalkerUniqueID();
				auto const listenerEntityID = acmp.getListenerEntityID();
				auto const listenerStreamIndex = acmp.getListenerUniqueID();
				auto const connectionCount = acmp.getConnectionCount();
				auto const flags = acmp.getFlags();
				answerCallback.invoke<controller::Interface::ConnectStreamHandler>(controllerInterface, model::StreamIdentification{ talkerEntityID, talkerStreamIndex }, model::StreamIdentification{ listenerEntityID, listenerStreamIndex }, connectionCount, flags, status);
				if (sniffed && delegate)
				{
					utils::invokeProtectedMethod(&controller::Delegate::onControllerConnectResponseSniffed, delegate, controllerInterface, model::StreamIdentification{ talkerEntityID, talkerStreamIndex }, model::StreamIdentification{ listenerEntityID, listenerStreamIndex }, connectionCount, flags, status);
				}
			} },
		// Disconnect RX response
		{ protocol::AcmpMessageType::DisconnectRxResponse.getValue(),
			[](controller::Delegate* const delegate, Interface const* const controllerInterface, LocalEntity::ControlStatus const status, protocol::Acmpdu const& acmp, LocalEntityImpl<>::AnswerCallback const& answerCallback, bool const sniffed)
			{
				auto const talkerEntityID = acmp.getTalkerEntityID();
				auto const talkerStreamIndex = acmp.getTalkerUniqueID();
				auto const listenerEntityID = acmp.getListenerEntityID();
				auto const listenerStreamIndex = acmp.getListenerUniqueID();
				auto const connectionCount = acmp.getConnectionCount();
				auto const flags = acmp.getFlags();
				answerCallback.invoke<controller::Interface::DisconnectStreamHandler>(controllerInterface, model::StreamIdentification{ talkerEntityID, talkerStreamIndex }, model::StreamIdentification{ listenerEntityID, listenerStreamIndex }, connectionCount, flags, status);
				if (sniffed && delegate)
				{
					utils::invokeProtectedMethod(&controller::Delegate::onControllerDisconnectResponseSniffed, delegate, controllerInterface, model::StreamIdentification{ talkerEntityID, talkerStreamIndex }, model::StreamIdentification{ listenerEntityID, listenerStreamIndex }, connectionCount, flags, status);
				}
			} },
		// Get RX state response
		{ protocol::AcmpMessageType::GetRxStateResponse.getValue(),
			[](controller::Delegate* const delegate, Interface const* const controllerInterface, LocalEntity::ControlStatus const status, protocol::Acmpdu const& acmp, LocalEntityImpl<>::AnswerCallback const& answerCallback, bool const sniffed)
			{
				auto const talkerEntityID = acmp.getTalkerEntityID();
				auto const talkerStreamIndex = acmp.getTalkerUniqueID();
				auto const listenerEntityID = acmp.getListenerEntityID();
				auto const listenerStreamIndex = acmp.getListenerUniqueID();
				auto const connectionCount = acmp.getConnectionCount();
				auto const flags = acmp.getFlags();
				answerCallback.invoke<controller::Interface::GetListenerStreamStateHandler>(controllerInterface, model::StreamIdentification{ talkerEntityID, talkerStreamIndex }, model::StreamIdentification{ listenerEntityID, listenerStreamIndex }, connectionCount, flags, status);
				if (sniffed && delegate)
				{
					utils::invokeProtectedMethod(&controller::Delegate::onGetListenerStreamStateResponseSniffed, delegate, controllerInterface, model::StreamIdentification{ talkerEntityID, talkerStreamIndex }, model::StreamIdentification{ listenerEntityID, listenerStreamIndex }, connectionCount, flags, status);
				}
			} },
		// Get TX connection response
		{ protocol::AcmpMessageType::GetTxConnectionResponse.getValue(),
			[](controller::Delegate* const /*delegate*/, Interface const* const controllerInterface, LocalEntity::ControlStatus const status, protocol::Acmpdu const& acmp, LocalEntityImpl<>::AnswerCallback const& answerCallback, bool const /*sniffed*/)
			{
				auto const talkerEntityID = acmp.getTalkerEntityID();
				auto const talkerStreamIndex = acmp.getTalkerUniqueID();
				auto const listenerEntityID = acmp.getListenerEntityID();
				auto const listenerStreamIndex = acmp.getListenerUniqueID();
				auto const connectionCount = acmp.getConnectionCount();
				auto const flags = acmp.getFlags();
				answerCallback.invoke<controller::Interface::GetTalkerStreamConnectionHandler>(controllerInterface, model::StreamIdentification{ talkerEntityID, talkerStreamIndex }, model::StreamIdentification{ listenerEntityID, listenerStreamIndex }, connectionCount, flags, status);
			} },
	};

	auto const& it = s_Dispatch.find(acmp.getMessageType().getValue());
	if (it == s_Dispatch.end())
	{
		// If this is a sniffed message, simply log we do not handle the message
		if (sniffed)
		{
			LOG_CONTROLLER_ENTITY_DEBUG(acmp.getTalkerEntityID(), "ACMP response {} not handled ({})", std::string(acmp.getMessageType()), utils::toHexString(acmp.getMessageType().getValue()));
		}
		// But if it's an expected response, this is an internal error since we sent a command and didn't implement the code to handle the response
		else
		{
			LOG_CONTROLLER_ENTITY_ERROR(acmp.getTalkerEntityID(), "Failed to process ACMP response: Unhandled message type {} ({})", std::string(acmp.getMessageType()), utils::toHexString(acmp.getMessageType().getValue()));
			utils::invokeProtectedHandler(onErrorCallback, LocalEntity::ControlStatus::InternalError);
		}
	}
	else
	{
		try
		{
			it->second(_controllerDelegate, &_controllerInterface, status, acmp, answerCallback, sniffed);
		}
		catch ([[maybe_unused]] std::exception const& e) // Mainly unpacking errors
		{
			LOG_CONTROLLER_ENTITY_ERROR(acmp.getTalkerEntityID(), "Failed to process ACMP response: {}", e.what());
			utils::invokeProtectedHandler(onErrorCallback, LocalEntity::ControlStatus::ProtocolError);
			return;
		}
	}
}

} // namespace controller
} // namespace entity
} // namespace avdecc
} // namespace la
