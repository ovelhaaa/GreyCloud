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
    freezeSubgroup = std::make_unique<SubgroupComponent>("Freeze");
    addAndMakeVisible(freezeSubgroup.get());
    addRotaryControl("diffusion", "Diffusion");
    addRotaryControl("grainScan", "Grain Scan");
    addRotaryControl("reverseMix", "Rev Mix");
    addToggleControl("freeze", "Freeze");
    addToggleControl("hardFreeze", "Hard Frz");
    addToggleControl("stereoCore", "Stereo Core");

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
    addFaderControl("inputGain", "In");
    addFaderControl("outputGain", "Out");
    
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
    addAndMakeVisible(wrapper->label);

    wrapper->attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getVTS(), paramID, wrapper->slider);

    rotaryControls.push_back(std::move(wrapper));
}

void CloudGreyVerbEditor::addFaderControl(const juce::String& paramID, const juce::String& name) {
    auto wrapper = std::make_unique<RotaryControl>();
    wrapper->slider.setName(paramID);
    wrapper->slider.setSliderStyle(juce::Slider::LinearHorizontal);
    wrapper->slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    wrapper->slider.setPopupDisplayEnabled(true, true, this);
    addAndMakeVisible(wrapper->slider);

    wrapper->label.setText(name, juce::dontSendNotification);
    wrapper->label.setJustificationType(juce::Justification::centredRight);
    wrapper->label.setFont(12.0f);
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

    if (auto* choiceParam = dynamic_cast<juce::AudioParameterChoice*>(audioProcessor.getVTS().getParameter(paramID)))
        wrapper->comboBox.addItemList(choiceParam->choices, 1);

    addAndMakeVisible(wrapper->comboBox);

    if (!hideLabel) {
        wrapper->label.setText(name, juce::dontSendNotification);
        wrapper->label.setJustificationType(juce::Justification::centred);
        wrapper->label.setFont(12.0f);
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
    float scale = juce::jlimit(0.6f, 2.0f, (float) bounds.getWidth() / 720.0f);
    auto scaled = [scale](float v) { return juce::roundToInt(v * scale); };
    auto labelKnobGap = [](int knobDiameter) { return juce::jmax(6, static_cast<int>(knobDiameter * 0.35f)); };

    auto header = bounds.removeFromTop(scaled(40));
    presetSelector.setBounds(header.removeFromLeft(scaled(220)).withSizeKeepingCentre(scaled(200), scaled(24)));
    if (auto* hq = getToggle("hqMode"))
        hq->button.setBounds(header.removeFromRight(scaled(80)).withSizeKeepingCentre(scaled(60), scaled(22)));

    auto footer = bounds.removeFromBottom(scaled(70));

    auto placeRotary = [scaled, labelKnobGap](RotaryControl* c, juce::Rectangle<int> b, int knobBaseSize, int labelHeight) {
        if (!c) return;
        int knobDiameter = scaled(knobBaseSize);
        int gap = labelKnobGap(knobDiameter);
        int totalHeight = knobDiameter + gap + scaled(labelHeight);
        auto centerBox = b.withSizeKeepingCentre(juce::jmax(knobDiameter, scaled(knobBaseSize + 10)), totalHeight);
        
        auto labelArea = centerBox.removeFromTop(scaled(labelHeight));
        c->label.setBounds(labelArea.expanded(scaled(20), 0));
        
        centerBox.removeFromTop(gap);
        c->slider.setBounds(centerBox.withSizeKeepingCentre(knobDiameter, knobDiameter));
    };

    auto fBtns = footer.removeFromLeft(scaled(130)).reduced(scaled(5));
    importButton.setBounds(fBtns.removeFromTop(fBtns.getHeight() / 2).reduced(scaled(2)));
    exportButton.setBounds(fBtns.reduced(scaled(2)));
    
    auto fPre = footer.removeFromLeft(scaled(140));
    placeRotary(getRotary("preDelay"), fPre.removeFromLeft(scaled(70)), 36, 16);
    auto fSync = fPre;
    if (auto* syncDiv = getChoice("syncDivision")) syncDiv->comboBox.setBounds(fSync.withSizeKeepingCentre(scaled(65), scaled(22)).translated(0, scaled(5)));
    if (auto* pdSync = getToggle("preDelaySync")) pdSync->button.setBounds(fSync.withSizeKeepingCentre(scaled(65), scaled(22)).translated(0, scaled(-20)));

    auto fWid = footer.removeFromLeft(scaled(80));
    placeRotary(getRotary("stereoWidth"), fWid, 36, 16);

    auto fCore = footer.removeFromLeft(scaled(100));
    if (auto* sc = getToggle("stereoCore")) sc->button.setBounds(fCore.withSizeKeepingCentre(scaled(90), scaled(22)));

    auto fInOut = footer.removeFromRight(scaled(120)).reduced(0, scaled(10));
    auto placeFader = [scaled](RotaryControl* c, juce::Rectangle<int> b) {
        if (!c) return;
        c->label.setBounds(b.removeFromLeft(scaled(30)));
        c->slider.setBounds(b.reduced(0, scaled(4)));
    };
    placeFader(getRotary("inputGain"), fInOut.removeFromTop(fInOut.getHeight() / 2));
    placeFader(getRotary("outputGain"), fInOut);

    auto macroRow = bounds.removeFromTop(scaled(120));
    int mw = macroRow.getWidth() / 4;
    
    auto m1 = macroRow.removeFromLeft(mw);
    placeRotary(getRotary("mix"), m1, 70, 20);

    auto m2 = macroRow.removeFromLeft(mw);
    auto m2Sync = m2.removeFromBottom(scaled(22));
    placeRotary(getRotary("size"), m2, 70, 20);
    if (auto* szSync = getToggle("sizeSync")) szSync->button.setBounds(m2Sync.withSizeKeepingCentre(scaled(50), scaled(18)));

    auto m3 = macroRow.removeFromLeft(mw);
    placeRotary(getRotary("feedback"), m3, 70, 20);

    auto m4 = macroRow;
    placeRotary(getRotary("texture"), m4, 70, 20);

    bounds.reduce(scaled(10), scaled(10));
    auto leftCol = bounds.removeFromLeft(juce::roundToInt(bounds.getWidth() * 0.32f));
    bounds.removeFromLeft(scaled(10));
    auto rightCol = bounds;

    cards[0]->setBounds(leftCol);
    auto c1Inner = leftCol.reduced(scaled(5));
    c1Inner.removeFromTop(scaled(20));
    
    auto freezeBounds = c1Inner.removeFromBottom(scaled(80));
    if (freezeSubgroup) freezeSubgroup->setBounds(freezeBounds);
    auto fzInner = freezeBounds.reduced(scaled(4));
    fzInner.removeFromTop(scaled(16));
    if (auto* frz = getToggle("freeze")) frz->button.setBounds(fzInner.removeFromTop(fzInner.getHeight()/2).withSizeKeepingCentre(scaled(70), scaled(22)));
    if (auto* hFrz = getToggle("hardFreeze")) hFrz->button.setBounds(fzInner.withSizeKeepingCentre(scaled(70), scaled(22)));
    
    int knobH = c1Inner.getHeight() / 3;
    placeRotary(getRotary("diffusion"), c1Inner.removeFromTop(knobH), 36, 16);
    placeRotary(getRotary("grainScan"), c1Inner.removeFromTop(knobH), 36, 16);
    placeRotary(getRotary("reverseMix"), c1Inner, 36, 16);

    int cardH = (rightCol.getHeight() - scaled(20)) / 3;
    
    auto c2 = rightCol.removeFromTop(cardH);
    cards[1]->setBounds(c2);
    auto c2Inner = c2.reduced(scaled(5));
    c2Inner.removeFromTop(scaled(20));
    placeRotary(getRotary("damping"), c2Inner.removeFromLeft(c2Inner.getWidth() / 3), 36, 16);
    placeRotary(getRotary("lowDamping"), c2Inner.removeFromLeft(c2Inner.getWidth() / 2), 36, 16);
    placeRotary(getRotary("tone"), c2Inner, 36, 16);

    rightCol.removeFromTop(scaled(10));
    auto c3 = rightCol.removeFromTop(cardH);
    cards[2]->setBounds(c3);
    auto c3Inner = c3.reduced(scaled(5));
    c3Inner.removeFromTop(scaled(20));
    placeRotary(getRotary("modDepth"), c3Inner.removeFromLeft(c3Inner.getWidth() / 2), 36, 16);
    placeRotary(getRotary("modRate"), c3Inner, 36, 16);

    rightCol.removeFromTop(scaled(10));
    auto c4 = rightCol;
    cards[3]->setBounds(c4);
    auto c4Inner = c4.reduced(scaled(5));
    c4Inner.removeFromTop(scaled(20));
    placeRotary(getRotary("shimmer"), c4Inner.removeFromLeft(c4Inner.getWidth() / 2), 36, 16);
    if (auto* sRat = getChoice("shimmerRatio")) sRat->comboBox.setBounds(c4Inner.withSizeKeepingCentre(scaled(90), scaled(24)));
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

