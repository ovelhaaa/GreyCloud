#include "PluginProcessor.h"
#include "PluginEditor.h"

CloudGreyVerbEditor::CloudGreyVerbEditor (CloudGreyVerbProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    setLookAndFeel(&customLookAndFeel);

    addAndMakeVisible(presetSelector);
    for (int i = 0; i < p.getNumPrograms(); ++i) {
        presetSelector.addItem(p.getProgramName(i), i + 1);
    }
    presetSelector.setSelectedId(p.getCurrentProgram() + 1, juce::dontSendNotification);
    presetSelector.onChange = [this, &p] { p.setCurrentProgram(presetSelector.getSelectedId() - 1); };

    addRotaryControl("mix", "Mix");
    addRotaryControl("size", "Size");
    addToggleControl("sizeSync", "Sync");
    addRotaryControl("feedback", "Feedback");
    addRotaryControl("texture", "Texture");

    cards.push_back(std::make_unique<CardComponent>("Grain / Diffuser"));
    addAndMakeVisible(cards.back().get());
    addRotaryControl("diffusion", "Diffusion");
    addRotaryControl("grainScan", "Grain Scan");
    addRotaryControl("reverseMix", "Rev Mix");
    addToggleControl("freeze", "Freeze");
    addToggleControl("hardFreeze", "Hard Frz");
    addToggleControl("stereoCore", "St Core");

    cards.push_back(std::make_unique<CardComponent>("Tone / Decay"));
    addAndMakeVisible(cards.back().get());
    addRotaryControl("damping", "Damping");
    addRotaryControl("lowDamping", "Low Damp");
    addRotaryControl("tone", "Tone");

    cards.push_back(std::make_unique<CardComponent>("Modulation"));
    addAndMakeVisible(cards.back().get());
    addRotaryControl("modDepth", "Depth");
    addRotaryControl("modRate", "Rate");

    cards.push_back(std::make_unique<CardComponent>("Shimmer"));
    addAndMakeVisible(cards.back().get());
    addRotaryControl("shimmer", "Shimmer");
    addChoiceControl("shimmerRatio", "Ratio", true);

    addRotaryControl("preDelay", "PreDelay");
    addToggleControl("preDelaySync", "Sync");
    addChoiceControl("syncDivision", "Div", true);
    addRotaryControl("stereoWidth", "Width");
    addRotaryControl("inputGain", "In");
    addRotaryControl("outputGain", "Out");
    
    addToggleControl("hqMode", "HQ");

    importButton.setButtonText("Load WASM JSON Preset...");
    importButton.onClick = [this] { loadJSONPreset(); };
    addAndMakeVisible(importButton);

    exportButton.setButtonText("Export to JSON...");
    exportButton.onClick = [this] { exportJSONPreset(); };
    addAndMakeVisible(exportButton);

    if (auto* pdSync = audioProcessor.getVTS().getParameter("preDelaySync"))
        pdSync->addListener(this);
    if (auto* sSync = audioProcessor.getVTS().getParameter("sizeSync"))
        sSync->addListener(this);

    updateSyncState();

    setResizable(true, true);
    getConstrainer()->setFixedAspectRatio(720.0 / 560.0);
    setSize (720, 560);
}

CloudGreyVerbEditor::~CloudGreyVerbEditor()
{
    if (auto* pdSync = audioProcessor.getVTS().getParameter("preDelaySync"))
        pdSync->removeListener(this);
    if (auto* sSync = audioProcessor.getVTS().getParameter("sizeSync"))
        sSync->removeListener(this);
    setLookAndFeel(nullptr);
}

void CloudGreyVerbEditor::updateSyncState()
{
    if (auto* pdSync = audioProcessor.getVTS().getRawParameterValue("preDelaySync")) {
        bool sync = *pdSync > 0.5f;
        if (auto* pd = getRotary("preDelay")) pd->slider.setEnabled(!sync);
        if (auto* sd = getChoice("syncDivision")) sd->comboBox.setEnabled(sync);
    }
    
    if (auto* sSync = audioProcessor.getVTS().getRawParameterValue("sizeSync")) {
        bool sync = *sSync > 0.5f;
        if (auto* sz = getRotary("size")) sz->slider.setEnabled(!sync);
    }
}

void CloudGreyVerbEditor::parameterValueChanged (int parameterIndex, float newValue)
{
    juce::MessageManager::callAsync([this]() { updateSyncState(); });
}

CloudGreyVerbEditor::RotaryControl* CloudGreyVerbEditor::getRotary(const juce::String& paramID)
{
    for (auto& c : rotaryControls) if (c->slider.getName() == paramID) return c.get();
    return nullptr;
}
CloudGreyVerbEditor::ToggleControl* CloudGreyVerbEditor::getToggle(const juce::String& paramID)
{
    for (auto& c : toggleControls) if (c->button.getName() == paramID) return c.get();
    return nullptr;
}
CloudGreyVerbEditor::ChoiceControl* CloudGreyVerbEditor::getChoice(const juce::String& paramID)
{
    for (auto& c : choiceControls) if (c->comboBox.getName() == paramID) return c.get();
    return nullptr;
}

void CloudGreyVerbEditor::addRotaryControl(const juce::String& paramID, const juce::String& name) {
    auto wrapper = std::make_unique<RotaryControl>();
    wrapper->slider.setName(paramID);
    wrapper->slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    wrapper->slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    wrapper->slider.setPopupDisplayEnabled(true, true, this);
    addAndMakeVisible(wrapper->slider);

    wrapper->label.setText(name, juce::dontSendNotification);
    wrapper->label.setJustificationType(juce::Justification::centred);
    wrapper->label.setFont(12.0f);
    wrapper->label.attachToComponent(&wrapper->slider, false);
    addAndMakeVisible(wrapper->label);

    wrapper->attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getVTS(), paramID, wrapper->slider);

    rotaryControls.push_back(std::move(wrapper));
}

void CloudGreyVerbEditor::addToggleControl(const juce::String& paramID, const juce::String& name) {
    auto wrapper = std::make_unique<ToggleControl>();
    wrapper->button.setName(paramID);
    wrapper->button.setButtonText(name);
    addAndMakeVisible(wrapper->button);

    wrapper->attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.getVTS(), paramID, wrapper->button);

    toggleControls.push_back(std::move(wrapper));
}

void CloudGreyVerbEditor::addChoiceControl(const juce::String& paramID, const juce::String& name, bool hideLabel) {
    auto wrapper = std::make_unique<ChoiceControl>();
    wrapper->comboBox.setName(paramID);
    addAndMakeVisible(wrapper->comboBox);

    if (!hideLabel) {
        wrapper->label.setText(name, juce::dontSendNotification);
        wrapper->label.setJustificationType(juce::Justification::centred);
        wrapper->label.setFont(12.0f);
        wrapper->label.attachToComponent(&wrapper->comboBox, false);
        addAndMakeVisible(wrapper->label);
    }

    wrapper->attachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        audioProcessor.getVTS(), paramID, wrapper->comboBox);

    choiceControls.push_back(std::move(wrapper));
}

void CloudGreyVerbEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour(24, 24, 28));
    g.setColour (juce::Colours::white);
    g.setFont (20.0f);
    g.drawText ("Nimbus Reverb", 20, 0, 150, 40, juce::Justification::centredLeft, true);
}

void CloudGreyVerbEditor::resized()
{
    auto bounds = getLocalBounds();

    auto header = bounds.removeFromTop(40);
    presetSelector.setBounds(header.withSizeKeepingCentre(200, 24));
    if (auto* hq = getToggle("hqMode"))
        hq->button.setBounds(header.removeFromRight(80).withSizeKeepingCentre(60, 22));

    auto footer = bounds.removeFromBottom(70);
    auto f1 = footer.removeFromLeft(footer.getWidth() / 6);
    if (auto* pd = getRotary("preDelay")) pd->slider.setBounds(f1.withSizeKeepingCentre(36, 36).translated(0, 10));
    
    auto f2 = footer.removeFromLeft(footer.getWidth() / 5);
    if (auto* syncDiv = getChoice("syncDivision")) syncDiv->comboBox.setBounds(f2.withSizeKeepingCentre(70, 22).translated(0, 5));
    if (auto* pdSync = getToggle("preDelaySync")) pdSync->button.setBounds(f2.withSizeKeepingCentre(70, 22).translated(0, -20));

    auto f3 = footer.removeFromLeft(footer.getWidth() / 4);
    if (auto* sw = getRotary("stereoWidth")) sw->slider.setBounds(f3.withSizeKeepingCentre(36, 36).translated(0, 10));

    auto f4 = footer.removeFromLeft(footer.getWidth() / 3);
    if (auto* ig = getRotary("inputGain")) ig->slider.setBounds(f4.removeFromLeft(f4.getWidth()/2).withSizeKeepingCentre(36, 36).translated(0, 10));
    if (auto* og = getRotary("outputGain")) og->slider.setBounds(f4.withSizeKeepingCentre(36, 36).translated(0, 10));

    auto fb = footer.reduced(10);
    importButton.setBounds(fb.removeFromTop(fb.getHeight() / 2).reduced(2));
    exportButton.setBounds(fb.reduced(2));

    auto macroRow = bounds.removeFromTop(120);
    int mw = macroRow.getWidth() / 4;
    
    auto m1 = macroRow.removeFromLeft(mw);
    if (auto* mix = getRotary("mix")) mix->slider.setBounds(m1.withSizeKeepingCentre(70, 70));

    auto m2 = macroRow.removeFromLeft(mw);
    if (auto* sz = getRotary("size")) sz->slider.setBounds(m2.withSizeKeepingCentre(70, 70));
    if (auto* szSync = getToggle("sizeSync")) szSync->button.setBounds(m2.removeFromBottom(22).withSizeKeepingCentre(50, 18));

    auto m3 = macroRow.removeFromLeft(mw);
    if (auto* fbControl = getRotary("feedback")) fbControl->slider.setBounds(m3.withSizeKeepingCentre(70, 70));

    auto m4 = macroRow;
    if (auto* tex = getRotary("texture")) tex->slider.setBounds(m4.withSizeKeepingCentre(70, 70));

    bounds.reduce(10, 10);
    int cw = bounds.getWidth() / 2;
    int ch = bounds.getHeight() / 2;

    auto placeRotary = [](RotaryControl* c, juce::Rectangle<int> b) { if (c) c->slider.setBounds(b.withSizeKeepingCentre(36, 36).translated(0, 10)); };
    auto placeToggle = [](ToggleControl* c, juce::Rectangle<int> b) { if (c) c->button.setBounds(b.withSizeKeepingCentre(60, 22)); };

    auto c1 = bounds.withSize(cw, ch).reduced(5);
    cards[0]->setBounds(c1);
    c1.removeFromTop(20);
    auto c1_top = c1.removeFromTop(c1.getHeight() / 2);
    placeRotary(getRotary("diffusion"), c1_top.removeFromLeft(c1_top.getWidth() / 3));
    placeRotary(getRotary("grainScan"), c1_top.removeFromLeft(c1_top.getWidth() / 2));
    placeRotary(getRotary("reverseMix"), c1_top);
    
    auto c1_bot = c1;
    placeToggle(getToggle("freeze"), c1_bot.removeFromLeft(c1_bot.getWidth() / 3));
    placeToggle(getToggle("hardFreeze"), c1_bot.removeFromLeft(c1_bot.getWidth() / 2));
    placeToggle(getToggle("stereoCore"), c1_bot);

    auto c2 = bounds.withTrimmedLeft(cw).withSize(cw, ch).reduced(5);
    cards[1]->setBounds(c2);
    c2.removeFromTop(20);
    placeRotary(getRotary("damping"), c2.removeFromLeft(c2.getWidth() / 3));
    placeRotary(getRotary("lowDamping"), c2.removeFromLeft(c2.getWidth() / 2));
    placeRotary(getRotary("tone"), c2);

    auto c3 = bounds.withTrimmedTop(ch).withSize(cw, ch).reduced(5);
    cards[2]->setBounds(c3);
    c3.removeFromTop(20);
    placeRotary(getRotary("modDepth"), c3.removeFromLeft(c3.getWidth() / 2));
    placeRotary(getRotary("modRate"), c3);

    auto c4 = bounds.withTrimmedTop(ch).withTrimmedLeft(cw).withSize(cw, ch).reduced(5);
    cards[3]->setBounds(c4);
    c4.removeFromTop(20);
    placeRotary(getRotary("shimmer"), c4.removeFromLeft(c4.getWidth() / 2));
    if (auto* sRat = getChoice("shimmerRatio")) sRat->comboBox.setBounds(c4.withSizeKeepingCentre(90, 24));
}

void CloudGreyVerbEditor::exportJSONPreset()
{
    fileChooser = std::make_unique<juce::FileChooser>("Save GreyCloud Preset JSON",
        juce::File::getSpecialLocation(juce::File::userDesktopDirectory).getChildFile("vst_preset.json"),
        "*.json");
        
    auto folderChooserFlags = juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles;
    
    fileChooser->launchAsync(folderChooserFlags, [this] (const juce::FileChooser& chooser)
    {
        auto file = chooser.getResult();
        if (file.isDirectory() || file.getFileName().isEmpty()) return;
        
        juce::DynamicObject::Ptr presetObj = new juce::DynamicObject();
        presetObj->setProperty("name", "VST Export");
        
        juce::DynamicObject::Ptr paramsObj = new juce::DynamicObject();
        auto* vts = &audioProcessor.getVTS();
        
        auto state = vts->copyState();
        for (auto child : state)
        {
            if (child.hasType("PARAM"))
            {
                auto id = child.getProperty("id").toString();
                double val = static_cast<double>(child.getProperty("value"));
                paramsObj->setProperty(id, val);
            }
        }
        
        presetObj->setProperty("params", juce::var(paramsObj.get()));
        
        juce::Array<juce::var> presetsArray;
        presetsArray.add(juce::var(presetObj.get()));
        
        juce::DynamicObject::Ptr rootObj = new juce::DynamicObject();
        rootObj->setProperty("app", "GreyCloud");
        rootObj->setProperty("version", 1);
        rootObj->setProperty("presets", juce::var(presetsArray));
        
        juce::FileOutputStream fos(file);
        if (fos.openedOk())
        {
            fos.setPosition(0);
            fos.truncate();
            juce::JSON::writeToStream(fos, juce::var(rootObj.get()));
        }
    });
}

void CloudGreyVerbEditor::loadJSONPreset()
{
    fileChooser = std::make_unique<juce::FileChooser>("Select GreyCloud Preset JSON",
        juce::File::getSpecialLocation(juce::File::userDesktopDirectory),
        "*.json");
        
    auto folderChooserFlags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
    
    fileChooser->launchAsync(folderChooserFlags, [this] (const juce::FileChooser& chooser)
    {
        auto file = chooser.getResult();
        if (!file.existsAsFile()) return;
        
        juce::var jsonObject = juce::JSON::parse(file);
        if (!jsonObject.isObject()) return;
        
        auto* obj = jsonObject.getDynamicObject();
        if (!(obj && obj->hasProperty("presets") && jsonObject["presets"].isArray())) return;

        auto* presetsArray = jsonObject["presets"].getArray();
        if (!presetsArray || presetsArray->isEmpty()) return;

        juce::PopupMenu m;
        for (int i = 0; i < presetsArray->size(); ++i)
        {
            auto presetVar = presetsArray->getReference(i);
            if (presetVar.isObject() && presetVar.getDynamicObject()->hasProperty("name"))
            {
                m.addItem(i + 1, presetVar.getDynamicObject()->getProperty("name").toString());
            }
        }
        
        if (m.getNumItems() == 0) return;
        
        // Pass jsonObject by value so its ref-count keeps the tree alive
        m.showMenuAsync(juce::PopupMenu::Options(), [this, jsonObject] (int result)
        {
            if (result <= 0) return;
            int idx = result - 1;
            auto* pArray = jsonObject["presets"].getArray();
            if (!pArray) return;
            auto presetVar = pArray->getReference(idx);
            if (presetVar.isObject() && presetVar.getDynamicObject()->hasProperty("params"))
            {
                auto paramsVar = presetVar.getDynamicObject()->getProperty("params");
                if (paramsVar.isObject())
                {
                    auto* paramsObj = paramsVar.getDynamicObject();
                    auto* vts = &audioProcessor.getVTS();
                    for (auto& prop : paramsObj->getProperties())
                    {
                        auto id = prop.name.toString();
                        if (auto* param = vts->getParameter(id))
                        {
                            float normalized = param->convertTo0to1(static_cast<float>(static_cast<double>(prop.value)));
                            param->setValueNotifyingHost(normalized);
                        }
                    }
                }
            }
        });
    });
}

