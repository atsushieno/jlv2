/*
    Copyright (c) 2014-2019  Michael Fisher <mfisher@kushview.net>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#ifndef JLV2_NUM_WORKERS
 #define JLV2_NUM_WORKERS 1
#endif

namespace jlv2 {

World::World()
{
   #if JUCE_MAC
    StringArray path;
    path.add (File ("/Library/Audio/Plug-Ins/LV2").getFullPathName());
    path.add (File::getSpecialLocation (File::userHomeDirectory)
        .getChildFile ("Library/Audio/Plug-Ins/LV2")
        .getFullPathName());
    path.trim();
    setenv ("LV2_PATH", path.joinIntoString(":").toRawUTF8(), 1);
    DBG("LV2_PATH = " << String (getenv ("LV2_PATH")));
   #endif

    world = lilv_world_new();

    lv2_InputPort   = lilv_new_uri (world, LV2_CORE__InputPort);
    lv2_OutputPort  = lilv_new_uri (world, LV2_CORE__OutputPort);
    lv2_AudioPort   = lilv_new_uri (world, LV2_CORE__AudioPort);
    lv2_AtomPort    = lilv_new_uri (world, LV2_ATOM__AtomPort);
    lv2_ControlPort = lilv_new_uri (world, LV2_CORE__ControlPort);
    lv2_EventPort   = lilv_new_uri (world, LV2_EVENT__EventPort);
    lv2_CVPort      = lilv_new_uri (world, LV2_CORE__CVPort);
    midi_MidiEvent  = lilv_new_uri (world, LV2_MIDI__MidiEvent);
    work_schedule   = lilv_new_uri (world, LV2_WORKER__schedule);
    work_interface  = lilv_new_uri (world, LV2_WORKER__interface);
    ui_CocoaUI      = lilv_new_uri (world, LV2_UI__CocoaUI);
    ui_X11UI        = lilv_new_uri (world, LV2_UI__X11UI);
    ui_JUCEUI       = lilv_new_uri (world, LVTK__JUCEUI);
    ui_UI           = lilv_new_uri (world, LV2_UI__UI);

    trueNode        = lilv_new_bool (world, true);
    falseNode       = lilv_new_bool (world, false);
    
    lilv_world_set_option (world, LILV_OPTION_DYN_MANIFEST, trueNode);

    lilv_world_load_all (world);
    suil = suil_host_new (ModuleUI::portWrite,
                          ModuleUI::portIndex, 0, 0);

    currentThread = 0;
    numThreads    = JLV2_NUM_WORKERS;
    for (int i = 0; i < numThreads; ++i)
    {
        threads.add (new WorkThread ("lv2_worker_ " + String(i + 1), 2048));
        threads.getLast()->setPriority (5);
    }
}

World::~World()
{
#define _node_free(n) lilv_node_free (const_cast<LilvNode*> (n))
    _node_free (lv2_InputPort);
    _node_free (lv2_OutputPort);
    _node_free (lv2_AudioPort);
    _node_free (lv2_AtomPort);
    _node_free (lv2_ControlPort);
    _node_free (lv2_EventPort);
    _node_free (lv2_CVPort);
    _node_free (midi_MidiEvent);
    _node_free (work_schedule);
    _node_free (work_interface);
    _node_free (ui_CocoaUI);
    _node_free (ui_X11UI);
    _node_free (ui_JUCEUI);

    lilv_world_free (world);
    world = nullptr;
    suil_host_free (suil);
    suil = nullptr;
}

Module* World::createModule (const String& uri)
{
    if (const LilvPlugin* plugin = getPlugin (uri))
        return new Module (*this, plugin);
    return nullptr;
}

void World::fillPluginDescription (const String& uri, PluginDescription& desc) const
{
    jassertfalse;
   #if 0
    if (const LilvPlugin* plugin = getPlugin (uri))
    {
        LV2PluginModel model (*const_cast<World*> (this), plugin);
        desc.category = model.getClassLabel();
        desc.descriptiveName = String();
        desc.fileOrIdentifier = uri;
        desc.hasSharedContainer = false;
        desc.isInstrument = model.getMidiPort() != LV2UI_INVALID_PORT_INDEX;
        desc.lastFileModTime = Time();
        desc.manufacturerName = model.getAuthorName();
        desc.name = model.getName();
        desc.numInputChannels  = model.getNumPorts (PortType::Audio, true);
        desc.numOutputChannels = model.getNumPorts (PortType::Audio, false);
        desc.pluginFormatName = String ("LV2");
        desc.uid = desc.fileOrIdentifier.hashCode();
        desc.version = String();
    }
   #endif
}

const LilvPlugin* World::getPlugin (const String& uri) const
{
    LilvNode* p (lilv_new_uri (world, uri.toUTF8()));
    const LilvPlugin* plugin = lilv_plugins_get_by_uri (getAllPlugins(), p);
    lilv_node_free (p);

    return plugin;
}

String World::getPluginName (const String& uri) const
{
    auto* uriNode = lilv_new_uri (world, uri.toRawUTF8());
    const auto* plugin = lilv_plugins_get_by_uri (
        lilv_world_get_all_plugins (world), uriNode);
    lilv_node_free (uriNode); uriNode = nullptr;

    String name;

    if (plugin != nullptr)
    {
        auto* nameNode = lilv_plugin_get_name (plugin);
        name = String::fromUTF8 (lilv_node_as_string (nameNode));
        lilv_node_free (nameNode);
    }

    return name;
}

void World::getSupportedPlugins (StringArray& list) const
{
    const LilvPlugins* plugins (lilv_world_get_all_plugins (world));
    LILV_FOREACH (plugins, iter, plugins)
    {
        const LilvPlugin* plugin = lilv_plugins_get (plugins, iter);
        const String uri = String::fromUTF8 (lilv_node_as_uri (lilv_plugin_get_uri (plugin)));
        if (isPluginSupported (uri))
            list.add (uri);
    }
}

const LilvPlugins* World::getAllPlugins() const
{
    return lilv_world_get_all_plugins (world);
}

WorkThread& World::getWorkThread()
{
    while (threads.size() < numThreads) {
        threads.add (new WorkThread ("LV2 Worker " + String(threads.size()), 2048));
        threads.getLast()->setPriority (5);
    }

    const int32 threadIndex = currentThread;
    if (++currentThread >= numThreads)
        currentThread = 0;

    return *threads.getUnchecked (threadIndex);
}

bool World::isFeatureSupported (const String& featureURI) const
{
   if (features.contains (featureURI))
      return true;

   if (featureURI == LV2_WORKER__schedule)
      return true;

   return false;
}

bool World::isPluginAvailable (const String& uri)
{
    return (getPlugin (uri) != nullptr);
}

bool World::isPluginSupported (const String& uri) const
{
    if (const LilvPlugin * plugin = getPlugin (uri))
        return isPluginSupported (plugin);
    return false;
}

bool World::isPluginSupported (const LilvPlugin* plugin) const
{
    // Required features support
    LilvNodes* nodes = lilv_plugin_get_required_features (plugin);
    LILV_FOREACH (nodes, iter, nodes)
    {
        const LilvNode* node (lilv_nodes_get (nodes, iter));
        if (! isFeatureSupported (CharPointer_UTF8 (lilv_node_as_uri (node)))) {
            return false; // Feature not supported
        }
    }

    lilv_nodes_free (nodes); nodes = nullptr;

    // Check this plugin's port types are supported
    const uint32 numPorts = lilv_plugin_get_num_ports (plugin);
    for (uint32 i = 0; i < numPorts; ++i)
    {
        // const LilvPort* port (lilv_plugin_get_port_by_index (plugin, i));
        // nothing here yet
    }

    return true;
}

}