#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "BinaryData.h"

#include <cmath>

namespace
{
class NimbusAnimatedLogo final : public juce::Component, private juce::Timer
{
public:
    NimbusAnimatedLogo()
    {
        setInterceptsMouseClicks(false, false);
        loadParticlesFromSvg();
        startTimerHz(30);
    }

    void paint(juce::Graphics& g) override
    {
        auto area = getLocalBounds().toFloat();
        g.setColour(juce::Colour(17, 17, 17));
        g.fillRoundedRectangle(area, 5.0f);

        if (particles.empty())
            return;

        auto logoArea = area.reduced(1.0f);
        const float drawScale = juce::jmin(logoArea.getWidth(), logoArea.getHeight()) / 1024.0f;
        const float xOffset = logoArea.getCentreX() - 512.0f * drawScale;
        const float yOffset = logoArea.getCentreY() - 512.0f * drawScale;
        const auto nowSeconds = (juce::Time::getMillisecondCounterHiRes() - startTimeMs) * 0.001;
        const auto gold = juce::Colour(221, 191, 114);

        for (const auto& particle : particles)
        {
            float alpha = 1.0f;
            float radiusScale = 1.0f;

            if (particle.animated)
            {
                const auto phase = std::fmod(nowSeconds - (double) particle.delaySeconds + 3.0, 3.0) / 3.0;
                const auto pulse = 0.5 - 0.5 * std::cos(phase * juce::MathConstants<double>::twoPi);
                alpha = (float) (1.0 - 0.5 * pulse);
                radiusScale = (float) (1.0 - 0.2 * pulse);
            }

            const auto radius = particle.radius * radiusScale * drawScale;
            const auto x = xOffset + particle.x * drawScale;
            const auto y = yOffset + particle.y * drawScale;

            g.setColour(gold.withAlpha(alpha));
            g.fillEllipse(x - radius, y - radius, radius * 2.0f, radius * 2.0f);
        }
    }

private:
    struct Particle
    {
        float x = 0.0f;
        float y = 0.0f;
        float radius = 0.0f;
        float delaySeconds = 0.0f;
        bool animated = false;
    };

    void loadParticlesFromSvg()
    {
        auto svg = juce::String::fromUTF8(BinaryData::nimbus_logo_custom_svg,
                                          BinaryData::nimbus_logo_custom_svgSize);
        juce::XmlDocument document(svg);

        if (auto root = document.getDocumentElement())
            parseElement(*root);
    }

    void parseElement(const juce::XmlElement& element)
    {
        if (element.hasTagName("circle"))
        {
            Particle particle;
            particle.x = (float) element.getDoubleAttribute("cx");
            particle.y = (float) element.getDoubleAttribute("cy");
            particle.radius = (float) element.getDoubleAttribute("r");
            particle.animated = element.getStringAttribute("class").contains("particle-organic");
            particle.delaySeconds = parseDelay(element.getStringAttribute("style"));
            particles.push_back(particle);
        }

        for (auto* child = element.getFirstChildElement(); child != nullptr; child = child->getNextElement())
            parseElement(*child);
    }

    void timerCallback() override
    {
        repaint();
    }

    static float parseDelay(const juce::String& style)
    {
        const auto delayKey = "animation-delay:";
        const auto delayStart = style.indexOf(delayKey);

        if (delayStart < 0)
            return 0.0f;

        return (float) style.substring(delayStart + juce::String(delayKey).length())
                            .trimStart()
                            .upToFirstOccurrenceOf("s", false, false)
                            .getDoubleValue();
    }

    std::vector<Particle> particles;
    double startTimeMs = juce::Time::getMillisecondCounterHiRes();
};
}

CloudGreyVerbEditor::CloudGreyVerbEditor (CloudGreyVerbProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    setLookAndFeel(&customLookAndFeel);
    nimbusLogo = std::make_unique<NimbusAnimatedLogo>();
    addAndMakeVisible(nimbusLogo.get());

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

    importButton.setButtonText("Import JSON");
    importButton.onClick = [this] { loadJSONPreset(); };
    addAndMakeVisible(importButton);

    exportButton.setButtonText("Export JSON");
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
    const auto backgroundColour = juce::Colour(24, 24, 28);
    const auto headerColour = juce::Colour(17, 17, 17);
    const auto accentColour = juce::Colour(221, 191, 114);
    const auto outlineColour = juce::Colour(42, 42, 47);
    const auto panelOutlineColour = juce::Colour(60, 60, 65);

    auto bounds = getLocalBounds();
    const float scale = juce::jlimit(0.6f, 2.0f, (float) bounds.getWidth() / 720.0f);
    auto scaled = [scale](float v) { return juce::roundToInt(v * scale); };
    auto header = bounds.removeFromTop(scaled(64));

    g.fillAll(backgroundColour);
    g.setColour(headerColour);
    g.fillRect(header);
    g.setColour(outlineColour);
    g.drawHorizontalLine(header.getBottom() - 1, 0.0f, (float) getWidth());

    auto textArea = juce::Rectangle<int>(header.getX() + scaled(82),
                                         header.getY() + scaled(13),
                                         scaled(170),
                                         scaled(38));
    g.setColour(juce::Colours::white);
    g.setFont(juce::Font(22.0f * scale, juce::Font::bold));
    g.drawText("NIMBUS", textArea.removeFromTop(scaled(25)), juce::Justification::centredLeft, true);

    g.setColour(accentColour.withAlpha(0.82f));
    g.setFont(juce::Font(9.0f * scale, juce::Font::bold));
    g.drawText("REVERB", textArea, juce::Justification::centredLeft, true);

    auto footer = getLocalBounds().removeFromBottom(scaled(82));
    auto footerLayout = footer.reduced(scaled(10), 0);
    auto presetGroupWidth = juce::roundToInt(footerLayout.getWidth() * 0.32f);

    auto drawFooterGroup = [&](juce::Rectangle<int> groupBounds, const juce::String& title)
    {
        groupBounds = groupBounds.reduced(scaled(4), scaled(5));
        g.setColour(panelOutlineColour.withAlpha(0.75f));
        g.drawRoundedRectangle(groupBounds.toFloat(), 4.0f, 1.0f);

        auto titleArea = groupBounds.removeFromTop(scaled(16));
        g.setColour(juce::Colour(34, 34, 39));
        g.fillRoundedRectangle(titleArea.toFloat().reduced(1.0f, 1.0f), 3.0f);
        g.setColour(accentColour);
        g.setFont(juce::Font(9.5f * scale, juce::Font::bold));
        g.drawText(title, titleArea, juce::Justification::centred, true);
    };

    drawFooterGroup(footerLayout.removeFromLeft(presetGroupWidth), "PRESETS");
    footerLayout.removeFromLeft(scaled(10));

    const auto groupGap = scaled(6);
    const auto rightWidth = footerLayout.getWidth();
    auto preDelayWidth = juce::roundToInt(rightWidth * 0.32f);
    auto stereoWidth = juce::roundToInt(rightWidth * 0.36f);

    drawFooterGroup(footerLayout.removeFromLeft(preDelayWidth), "PRE DELAY");
    footerLayout.removeFromLeft(groupGap);
    drawFooterGroup(footerLayout.removeFromLeft(stereoWidth), "STEREO FIELD");
    footerLayout.removeFromLeft(groupGap);
    drawFooterGroup(footerLayout, "GAIN");
}

void CloudGreyVerbEditor::resized()
{
    auto bounds = getLocalBounds();
    float scale = juce::jlimit(0.6f, 2.0f, (float) bounds.getWidth() / 720.0f);
    auto scaled = [scale](float v) { return juce::roundToInt(v * scale); };
    const auto controlLabelGap = scaled(3);

    auto header = bounds.removeFromTop(scaled(64));
    if (nimbusLogo != nullptr)
        nimbusLogo->setBounds(header.getX() + scaled(14),
                              header.getCentreY() - scaled(25),
                              scaled(50),
                              scaled(50));

    if (auto* hq = getToggle("hqMode"))
        hq->button.setBounds(header.removeFromRight(scaled(80)).withSizeKeepingCentre(scaled(60), scaled(22)));
    presetSelector.setBounds(header.removeFromRight(scaled(220)).withSizeKeepingCentre(scaled(200), scaled(24)));

    auto footer = bounds.removeFromBottom(scaled(82));

    auto placeRotary = [scaled, controlLabelGap](RotaryControl* c, juce::Rectangle<int> b, int knobBaseSize, int labelHeight) {
        if (!c) return;
        int knobDiameter = scaled(knobBaseSize);
        int gap = controlLabelGap;
        int totalHeight = knobDiameter + gap + scaled(labelHeight);
        auto centerBox = b.withSizeKeepingCentre(juce::jmax(knobDiameter, scaled(knobBaseSize + 10)), totalHeight);
        
        auto labelArea = centerBox.removeFromTop(scaled(labelHeight));
        c->label.setBounds(labelArea.expanded(scaled(20), 0));
        
        centerBox.removeFromTop(gap);
        c->slider.setBounds(centerBox.withSizeKeepingCentre(knobDiameter, knobDiameter));
    };

    auto footerLayout = footer.reduced(scaled(10), 0);
    auto presetGroupWidth = juce::roundToInt(footerLayout.getWidth() * 0.32f);

    auto fBtns = footerLayout.removeFromLeft(presetGroupWidth).reduced(scaled(4), scaled(5)).withTrimmedTop(scaled(16));
    importButton.setBounds(fBtns.removeFromTop(fBtns.getHeight() / 2).reduced(scaled(3)));
    exportButton.setBounds(fBtns.reduced(scaled(3)));
    footerLayout.removeFromLeft(scaled(10));

    const auto groupGap = scaled(6);
    const auto rightWidth = footerLayout.getWidth();
    auto preDelayWidth = juce::roundToInt(rightWidth * 0.32f);
    auto stereoGroupWidth = juce::roundToInt(rightWidth * 0.36f);
    
    auto fPre = footerLayout.removeFromLeft(preDelayWidth).reduced(scaled(4), scaled(5));
    auto fPreControls = fPre.withTrimmedTop(scaled(16));
    placeRotary(getRotary("preDelay"), fPreControls.removeFromLeft(scaled(60)), 34, 14);
    fPreControls.removeFromLeft(scaled(2));
    auto pdSyncTop = fPreControls.removeFromTop(fPreControls.getHeight() / 2);
    if (auto* pdSync = getToggle("preDelaySync")) pdSync->button.setBounds(pdSyncTop.withSizeKeepingCentre(scaled(56), scaled(20)));
    if (auto* syncDiv = getChoice("syncDivision")) syncDiv->comboBox.setBounds(fPreControls.withSizeKeepingCentre(scaled(56), scaled(20)));

    footerLayout.removeFromLeft(groupGap);
    auto fStereo = footerLayout.removeFromLeft(stereoGroupWidth).reduced(scaled(4), scaled(5));
    auto fStereoControls = fStereo.withTrimmedTop(scaled(16));
    placeRotary(getRotary("stereoWidth"), fStereoControls.removeFromLeft(scaled(66)), 34, 14);
    if (auto* sc = getToggle("stereoCore")) sc->button.setBounds(fStereoControls.withSizeKeepingCentre(scaled(88), scaled(20)));

    footerLayout.removeFromLeft(groupGap);
    auto fInOut = footerLayout.reduced(scaled(5), scaled(5)).withTrimmedTop(scaled(16));
    auto placeFader = [scaled](RotaryControl* c, juce::Rectangle<int> b) {
        if (!c) return;
        c->label.setBounds(b.removeFromLeft(scaled(30)));
        c->slider.setBounds(b.reduced(0, scaled(4)));
    };
    placeFader(getRotary("inputGain"), fInOut.removeFromTop(fInOut.getHeight() / 2));
    placeFader(getRotary("outputGain"), fInOut);

    auto macroRow = bounds.removeFromTop(scaled(118));
    int mw = macroRow.getWidth() / 4;
    
    auto m1 = macroRow.removeFromLeft(mw);
    placeRotary(getRotary("mix"), m1.withTrimmedBottom(scaled(22)), 70, 20);

    auto m2 = macroRow.removeFromLeft(mw);
    auto m2Sync = m2.removeFromBottom(scaled(22));
    placeRotary(getRotary("size"), m2, 70, 20);
    if (auto* szSync = getToggle("sizeSync")) szSync->button.setBounds(m2Sync.withSizeKeepingCentre(scaled(50), scaled(18)));

    auto m3 = macroRow.removeFromLeft(mw);
    placeRotary(getRotary("feedback"), m3.withTrimmedBottom(scaled(22)), 70, 20);

    auto m4 = macroRow;
    placeRotary(getRotary("texture"), m4.withTrimmedBottom(scaled(22)), 70, 20);

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

