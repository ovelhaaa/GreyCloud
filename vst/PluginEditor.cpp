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
        startTimerHz(24);
    }

    void paint(juce::Graphics& g) override
    {
        auto area = getLocalBounds().toFloat();
        juce::ColourGradient halo(NimbusStyle::gold.withAlpha(0.045f), area.getCentreX(), area.getCentreY(),
                                  NimbusStyle::gold.withAlpha(0.0f), area.getRight(), area.getCentreY(), true);
        g.setGradientFill(halo);
        g.fillEllipse(area);

        if (particles.empty())
            return;

        auto logoArea = area.reduced(1.0f);
        const float drawScale = juce::jmin(logoArea.getWidth(), logoArea.getHeight()) / 820.0f;
        const float xOffset = logoArea.getCentreX() - 505.0f * drawScale;
        const float yOffset = logoArea.getCentreY() - 527.0f * drawScale;
        const auto nowSeconds = (juce::Time::getMillisecondCounterHiRes() - startTimeMs) * 0.001;
        const auto gold = NimbusStyle::gold;

        for (const auto& particle : particles)
        {
            float alpha = 1.0f;
            float radiusScale = 1.0f;

            if (particle.animated)
            {
                const auto period = 5.0 + particle.radius * 0.17;
                const auto phase = (nowSeconds - particle.delaySeconds + particle.x * 0.013) / period;
                const auto pulse = 0.5 - 0.5 * std::cos(phase * juce::MathConstants<double>::twoPi);
                alpha = (float) (1.0 - 0.12 * pulse);
                radiusScale = (float) (1.0 - 0.025 * pulse);
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
        if (isShowing()) repaint();
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
    syncFeedback.setJustificationType(juce::Justification::centred);
    syncFeedback.setFont(9.0f);
    syncFeedback.setTooltip("Shared by Pre-Delay Sync and Size Sync.");
    addAndMakeVisible(syncFeedback);
    previousPreset.onClick = [this] { audioProcessor.setCurrentProgram((audioProcessor.getCurrentProgram() + audioProcessor.getNumPrograms() - 1) % audioProcessor.getNumPrograms()); };
    nextPreset.onClick = [this] { audioProcessor.setCurrentProgram((audioProcessor.getCurrentProgram() + 1) % audioProcessor.getNumPrograms()); };
    addAndMakeVisible(previousPreset); addAndMakeVisible(nextPreset);

    addRotaryControl("mix", "Mix");
    addRotaryControl("size", "Size");
    addToggleControl("sizeSync", "Sync");
    addRotaryControl("feedback", "Feedback");
    addRotaryControl("texture", "Texture");

    cards.push_back(std::make_unique<CardComponent>("Grain / Diffusion", 224.0f));
    addAndMakeVisible(cards.back().get());
    freezeSubgroup = std::make_unique<SubgroupComponent>();
    addAndMakeVisible(freezeSubgroup.get());
    addRotaryControl("diffusion", "Diffusion");
    addRotaryControl("grainScan", "Grain Scan");
    addRotaryControl("reverseMix", "Reverse");
    addToggleControl("freeze", "Freeze");
    addToggleControl("hardFreeze", "Hard Freeze");
    addToggleControl("stereoCore", "Stereo Core");

    cards.push_back(std::make_unique<CardComponent>("Tone / Decay", 466.0f));
    addAndMakeVisible(cards.back().get());
    addRotaryControl("damping", "Damping");
    addRotaryControl("lowDamping", "Low Cut");
    addRotaryControl("tone", "Tone");

    cards.push_back(std::make_unique<CardComponent>("Modulation", 228.0f));
    addAndMakeVisible(cards.back().get());
    addRotaryControl("modDepth", "Depth");
    addRotaryControl("modRate", "Rate");

    cards.push_back(std::make_unique<CardComponent>("Shimmer", 228.0f));
    addAndMakeVisible(cards.back().get());
    addRotaryControl("shimmer", "Amount");
    addRotaryControl("shimmerRatio", "Ratio");

    addRotaryControl("preDelay", "Pre-Delay");
    addToggleControl("preDelaySync", "Sync");
    addChoiceControl("syncDivision", "Div", true);
    addRotaryControl("stereoWidth", "Width");
    addFaderControl("inputGain", "Input");
    addFaderControl("outputGain", "Output");
    
    addToggleControl("hqMode", "HQ");

    importButton.setButtonText("import");
    importButton.onClick = [this] { loadJSONPreset(); };
    addAndMakeVisible(importButton);

    exportButton.setButtonText("export");
    exportButton.onClick = [this] { exportJSONPreset(); };
    addAndMakeVisible(exportButton);
    importButton.setTooltip("Import a Nimbus or GreyCloud JSON preset file.");
    exportButton.setTooltip("Export presets as a JSON file.");
    previousPreset.setTooltip("Previous preset");
    nextPreset.setTooltip("Next preset");
    syncFeedback.setColour(juce::Label::textColourId, NimbusStyle::secondary);
    tooltipWindow = std::make_unique<juce::TooltipWindow>(this, 700);
    startTimerHz(5);

    if (auto* pdSync = audioProcessor.getVTS().getParameter("preDelaySync"))
        pdSync->addListener(this);
    if (auto* sSync = audioProcessor.getVTS().getParameter("sizeSync"))
        sSync->addListener(this);

    updateSyncState();
    timerCallback();

    setResizable(true, true);
    setResizeLimits(504, 392, 1440, 1120);
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
    if (auto* parameter = audioProcessor.getVTS().getParameter(paramID)) {
        wrapper->slider.setDoubleClickReturnValue(true,
            parameter->convertFrom0to1(parameter->getDefaultValue()));
        wrapper->slider.textFromValueFunction = [parameter] (double v) { return parameter->getText(parameter->convertTo0to1((float) v), 32); };
    }
    if (paramID == "size")
        wrapper->slider.textFromValueFunction = [this] (double v) {
            const auto* scale = audioProcessor.getVTS().getRawParameterValue("sizeScale");
            const auto ms = CloudGreyVerb::sizeToSeconds((float) v, scale != nullptr ? scale->load() : 1.0f) * 1000.0f;
            return juce::String(juce::roundToInt((float) v * 100.0f)) + juce::String::fromUTF8 (" %  \xE2\x89\x88 ") + juce::String(juce::roundToInt(ms)) + " ms";
        };
    addAndMakeVisible(wrapper->slider);

    wrapper->label.setText(name.toLowerCase(), juce::dontSendNotification);
    wrapper->label.setJustificationType(juce::Justification::centred);
    wrapper->label.setFont(NimbusStyle::label);
    wrapper->label.setColour(juce::Label::textColourId, NimbusStyle::text.withAlpha(0.86f));
    if (name == "Texture") wrapper->slider.setTooltip("Changes the cloud from tighter, grainier detail to a smoother smear.");
    if (name == "Diffusion") wrapper->slider.setTooltip("Controls how quickly reflections blend into a dense reverb field.");
    if (name == "Low Cut") wrapper->slider.setTooltip("Removes low frequencies from the feedback tail.");
    if (name == "Grain Scan") wrapper->slider.setTooltip("Moves the granular read position through captured history.");
    if (name == "Reverse") wrapper->slider.setTooltip("Blends reverse-moving grains into the cloud.");
    addAndMakeVisible(wrapper->label);

    wrapper->attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getVTS(), paramID, wrapper->slider);

    if (paramID == "shimmerRatio")
        if (auto* choice = dynamic_cast<juce::AudioParameterChoice*>(audioProcessor.getVTS().getParameter(paramID)))
        {
            // The existing choice parameter remains authoritative; the UI snaps to its indices.
            wrapper->slider.setRange(0.0, (double) choice->choices.size() - 1.0, 1.0);
            auto* slider = &wrapper->slider;
            auto* label = &wrapper->label;
            slider->onValueChange = [slider, label, choice] {
                const auto text = choice->choices[juce::jlimit(0, choice->choices.size() - 1,
                                                              juce::roundToInt(slider->getValue()))];
                slider->setTooltip(text);
                label->setText(text.toLowerCase(), juce::dontSendNotification);
            };
            slider->onValueChange();
        }

    rotaryControls.push_back(std::move(wrapper));
}

void CloudGreyVerbEditor::addFaderControl(const juce::String& paramID, const juce::String& name) {
    auto wrapper = std::make_unique<RotaryControl>();
    wrapper->slider.setName(paramID);
    wrapper->slider.setSliderStyle(juce::Slider::LinearHorizontal);
    wrapper->slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    wrapper->slider.setPopupDisplayEnabled(true, true, this);
    if (auto* parameter = audioProcessor.getVTS().getParameter(paramID)) {
        wrapper->slider.setDoubleClickReturnValue(true,
            parameter->convertFrom0to1(parameter->getDefaultValue()));
        wrapper->slider.textFromValueFunction = [parameter] (double v) { return parameter->getText(parameter->convertTo0to1((float) v), 32); };
    }
    addAndMakeVisible(wrapper->slider);

    wrapper->label.setText(name.toLowerCase(), juce::dontSendNotification);
    wrapper->label.setJustificationType(juce::Justification::centredRight);
    wrapper->label.setFont(NimbusStyle::label);
    wrapper->label.setColour(juce::Label::textColourId, NimbusStyle::text.withAlpha(0.86f));
    addAndMakeVisible(wrapper->label);

    wrapper->attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getVTS(), paramID, wrapper->slider);

    rotaryControls.push_back(std::move(wrapper));
}

void CloudGreyVerbEditor::addToggleControl(const juce::String& paramID, const juce::String& name) {
    auto wrapper = std::make_unique<ToggleControl>();
    wrapper->button.setName(paramID);
    wrapper->button.setButtonText(paramID == "hqMode" ? "HQ" : name.toLowerCase());
    if (paramID == "freeze") wrapper->button.setTooltip("Holds the current cloud while dry input continues normally.");
    if (paramID == "hardFreeze") wrapper->button.setTooltip("Stops new material entering the frozen reverb state.");
    if (paramID == "hqMode") wrapper->button.setTooltip("Processes at 2x internal sample rate for higher quality at a higher CPU cost.");
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
        wrapper->label.setText(name.toLowerCase(), juce::dontSendNotification);
        wrapper->label.setJustificationType(juce::Justification::centred);
        wrapper->label.setFont(NimbusStyle::label);
        addAndMakeVisible(wrapper->label);
    }

    wrapper->attachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        audioProcessor.getVTS(), paramID, wrapper->comboBox);

    choiceControls.push_back(std::move(wrapper));
}

void CloudGreyVerbEditor::paint (juce::Graphics& g)
{
    const float scale = (float) getWidth() / 720.0f;
    g.fillAll(NimbusStyle::background);
    g.setColour(NimbusStyle::deep);
    g.fillRect(0.0f, 0.0f, (float) getWidth(), 84.0f * scale);
    g.setColour(NimbusStyle::border);
    g.drawLine(0.0f, 84.0f * scale, (float) getWidth(), 84.0f * scale);
    g.setColour(NimbusStyle::text);
    brandTitle.draw(g);
    g.setColour(NimbusStyle::gold.withAlpha(0.72f));
    brandDescriptor.draw(g);
    g.setColour(NimbusStyle::deep.withAlpha(0.3f));
    g.fillRect(0.0f, 472.0f * scale, (float) getWidth(), 88.0f * scale);
    g.setColour(NimbusStyle::border);
    g.drawLine(10.0f * scale, 472.0f * scale, 710.0f * scale, 472.0f * scale);
    // Shared offset aligns the first utility separator with the Hard Freeze button.
    for (float x : { 129.0f, 316.0f, 524.0f })
        g.drawLine(x * scale, 485.0f * scale, x * scale, 550.0f * scale);
    g.setColour(NimbusStyle::secondary);
    g.setFont(NimbusStyle::captionFont(NimbusStyle::caption * scale));
    static const juce::String titles[] { "PRESETS", "STEREO FIELD", "TEMPO / PRE-DELAY", "GAIN" };
    constexpr float edges[] { 10.0f, 129.0f, 316.0f, 524.0f, 710.0f };
    for (int i = 0; i < 4; ++i)
        g.drawText(titles[i], juce::Rectangle<float>(edges[i] * scale, 478.0f * scale,
                                                   (edges[i + 1] - edges[i]) * scale, 14.0f * scale),
                   juce::Justification::centred);


}

void CloudGreyVerbEditor::timerCallback()
{
    if (freezeSubgroup)
        freezeSubgroup->setActivity(getToggle("freeze")->button.getToggleState(),
                                    getToggle("hardFreeze")->button.getToggleState());
    presetSelector.setSelectedId(audioProcessor.getCurrentProgram() + 1, juce::dontSendNotification);
    const auto* division = audioProcessor.getVTS().getRawParameterValue("syncDivision");
    const int index = juce::jlimit (0, static_cast<int> (TempoSyncUtils::kDivisionNames.size()) - 1,
                                    division != nullptr ? juce::roundToInt (division->load()) : 7);
    const float bpm = audioProcessor.getDisplayBpm();
    const auto ms = TempoSyncUtils::getMsFromBpm (bpm, index);
    syncFeedback.setText (juce::String (juce::roundToInt (bpm)) + juce::String::fromUTF8 (" BPM \xE2\x80\xA2 ")
                          + TempoSyncUtils::kDivisionNames[index] + " = "
                          + juce::String (juce::roundToInt (ms)) + " ms", juce::dontSendNotification);
}

void CloudGreyVerbEditor::resized()
{
    auto bounds = getLocalBounds();
    float scale = (float) bounds.getWidth() / 720.0f;
    auto scaled = [scale](float v) { return juce::roundToInt(v * scale); };
    const auto controlLabelGap = scaled(0);
    brandTitle.clear();
    brandTitle.addLineOfText(NimbusStyle::regularFont(23.0f * scale).withExtraKerningFactor(0.13f), "NIMBUS", 100.0f * scale, 43.0f * scale);
    brandDescriptor.clear();
    brandDescriptor.addLineOfText(NimbusStyle::captionFont(8.5f * scale).withExtraKerningFactor(0.24f), "REVERB", 100.0f * scale, 60.0f * scale);


    auto header = bounds.removeFromTop(scaled(84));
    if (nimbusLogo != nullptr)
        nimbusLogo->setBounds(header.getX() + scaled(9),
                              header.getCentreY() - scaled(40),
                              scaled(80),
                              scaled(80));

    const auto headerControlHeight = scaled(22);
    if (auto* hq = getToggle("hqMode"))
        hq->button.setBounds(header.removeFromRight(scaled(66)).withSizeKeepingCentre(scaled(42), headerControlHeight));
    auto presetArea = header.removeFromRight(scaled(240)).withSizeKeepingCentre(scaled(240), headerControlHeight);
    previousPreset.setBounds(presetArea.removeFromLeft(scaled(24)).withSizeKeepingCentre(scaled(22), headerControlHeight));
    nextPreset.setBounds(presetArea.removeFromRight(scaled(24)).withSizeKeepingCentre(scaled(22), headerControlHeight));
    presetSelector.setBounds(presetArea.reduced(scaled(2), 0));

    bounds.removeFromBottom(scaled(88));

    auto placeRotary = [scaled, controlLabelGap](RotaryControl* c, juce::Rectangle<int> b, int knobBaseSize, int labelHeight) {
        if (!c) return;
        int knobDiameter = scaled(knobBaseSize);
        int gap = controlLabelGap;
        int totalHeight = knobDiameter + gap + scaled(labelHeight);
        auto centerBox = b.withSizeKeepingCentre(juce::jmax(knobDiameter, scaled(knobBaseSize + 10)), totalHeight);
        
        c->slider.setBounds(centerBox.removeFromTop(knobDiameter).withSizeKeepingCentre(knobDiameter, knobDiameter));
        centerBox.removeFromTop(gap);
        c->label.setBounds(centerBox.withSizeKeepingCentre(juce::jmin(b.getWidth(), scaled(knobBaseSize + 50)), scaled(labelHeight)));

    };

    auto rect = [scaled](int x, int y, int w, int h) { return juce::Rectangle<int>(scaled(x), scaled(y), scaled(w), scaled(h)); };
    importButton.setBounds(rect(14, 498, 102, 22));
    exportButton.setBounds(rect(14, 526, 102, 22));
    placeRotary(getRotary("preDelay"), rect(323, 495, 70, 57), 34, 14);
    const auto* preDelay = getRotary("preDelay");
    const auto preDelayCentreY = preDelay->slider.getBounds().getCentreY();
    getToggle("preDelaySync")->button.setBounds(scaled(402), preDelayCentreY - scaled(10), scaled(52), scaled(20));
    getChoice("syncDivision")->comboBox.setBounds(scaled(460), preDelayCentreY - scaled(10), scaled(52), scaled(20));
    syncFeedback.setBounds(scaled(393), preDelay->label.getY(), scaled(123), preDelay->label.getHeight());
    placeRotary(getRotary("stereoWidth"), rect(134, 495, 68, 57), 34, 14);
    getToggle("stereoCore")->button.setBounds(rect(209, 514, 91, 22));
    auto placeFader = [&](RotaryControl* c, int y) {
        c->label.setBounds(rect(530, y, 43, 23));
        c->slider.setBounds(rect(581, y, 125, 23));
    };
    placeFader(getRotary("inputGain"), 496);
    placeFader(getRotary("outputGain"), 525);
    for (auto& c : rotaryControls) c->label.setFont(NimbusStyle::controlFont(NimbusStyle::label * scale));
    syncFeedback.setFont(NimbusStyle::regularFont(9.0f * scale));

    auto macroRow = bounds.removeFromTop(scaled(114));
    int mw = macroRow.getWidth() / 4;
    
    for (const auto& id : { "mix", "size", "feedback", "texture" })
        placeRotary(getRotary(id), macroRow.removeFromLeft(mw), 70, 18);

    // Keep the size label on the shared baseline; Sync shares that same label row.
    if (auto* size = getRotary("size"))
        if (auto* sync = getToggle("sizeSync"))
        {
            const auto labelBounds = size->label.getBounds();
            const auto labelWidth = scaled(30);
            const auto gap = scaled(6);
            const auto buttonWidth = scaled(42);
            const auto groupX = size->slider.getBounds().getCentreX() - (labelWidth + gap + buttonWidth) / 2;
            size->label.setBounds(groupX, labelBounds.getY(), labelWidth, labelBounds.getHeight());
            sync->button.setBounds(groupX + labelWidth + gap, labelBounds.getY(), buttonWidth, labelBounds.getHeight());
        }

    bounds.reduce(scaled(10), scaled(10));
    auto leftCol = bounds.removeFromLeft(juce::roundToInt(bounds.getWidth() * 0.32f));
    bounds.removeFromLeft(scaled(10));
    auto rightCol = bounds;

    const int cardH = (rightCol.getHeight() - scaled(20)) / 3;
    leftCol.setHeight(cardH * 3 + scaled(10));
    cards[0]->setBounds(leftCol);
    auto c1Inner = leftCol.reduced(scaled(5));
    c1Inner.removeFromTop(scaled(20));
    
    auto freezeBounds = c1Inner.removeFromBottom(scaled(48));
    if (freezeSubgroup) freezeSubgroup->setBounds(freezeBounds);
    auto fzInner = freezeBounds.reduced(scaled(4));
    auto softArea = fzInner.removeFromLeft(fzInner.getWidth() / 2).reduced(scaled(2), 0);
    auto hardArea = fzInner.reduced(scaled(2), 0);
    if (auto* frz = getToggle("freeze")) frz->button.setBounds(softArea.withSizeKeepingCentre(scaled(88), scaled(22)));
    if (auto* hFrz = getToggle("hardFreeze")) hFrz->button.setBounds(hardArea.withSizeKeepingCentre(scaled(88), scaled(22)));
    
    int knobH = c1Inner.getHeight() / 3;
    placeRotary(getRotary("diffusion"), c1Inner.removeFromTop(knobH), 36, 16);
    placeRotary(getRotary("grainScan"), c1Inner.removeFromTop(knobH), 36, 16);
    placeRotary(getRotary("reverseMix"), c1Inner, 36, 16);

    
    auto c2 = rightCol.removeFromTop(cardH);
    cards[1]->setBounds(c2);
    auto c2Inner = c2.reduced(scaled(5));
    c2Inner.removeFromTop(scaled(20));
    // Align the outer tone controls with the centres of the two zones below.
    const auto lowerColumnWidth = (c2.getWidth() - scaled(10)) / 2;
    const auto leftCentre = c2.getX() + lowerColumnWidth / 2;
    const auto rightCentre = c2.getRight() - lowerColumnWidth / 2;
    auto toneSlot = [&](int centreX) {
        return juce::Rectangle<int>(centreX - scaled(48), c2Inner.getY(), scaled(96), c2Inner.getHeight());
    };
    placeRotary(getRotary("tone"), toneSlot(leftCentre), 36, 16);
    placeRotary(getRotary("lowDamping"), toneSlot(c2.getCentreX()), 36, 16);
    placeRotary(getRotary("damping"), toneSlot(rightCentre), 36, 16);

    rightCol.removeFromTop(scaled(10));
    // Two vertical zones retain the existing controls and parameter attachments.
    auto lowerRow = rightCol.withHeight(cardH * 2);
    auto c3 = lowerRow.removeFromLeft((lowerRow.getWidth() - scaled(10)) / 2);
    lowerRow.removeFromLeft(scaled(10));
    auto c4 = lowerRow;
    cards[2]->setBounds(c3);
    cards[3]->setBounds(c4);

    auto c3Inner = c3.reduced(scaled(5));
    c3Inner.removeFromTop(scaled(20));
    placeRotary(getRotary("modDepth"), c3Inner.removeFromTop(c3Inner.getHeight() / 2), 36, 16);
    placeRotary(getRotary("modRate"), c3Inner, 36, 16);

    auto c4Inner = c4.reduced(scaled(5));
    c4Inner.removeFromTop(scaled(20));
    placeRotary(getRotary("shimmer"), c4Inner.removeFromTop(c4Inner.getHeight() / 2), 36, 16);
    placeRotary(getRotary("shimmerRatio"), c4Inner, 36, 16);
    if (auto* ratio = getRotary("shimmerRatio"))
        ratio->label.setBounds(ratio->label.getBounds().withX(c4Inner.getX()).withWidth(c4Inner.getWidth()));

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
        
        juce::FileOutputStream fos(file);
        if (fos.openedOk())
        {
            fos.setPosition(0);
            fos.truncate();
            juce::JSON::writeToStream(fos, audioProcessor.serializePresetJson());
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
        const auto app = obj->getProperty("app").toString();
        if (app != "GreyCloud" && app != "Nimbus") return;
        if (static_cast<int>(obj->getProperty("version")) != 1) return;

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
            // The processor owns schema and transactional validation so file
            // imports cannot differ from the testable JSON contract.
            audioProcessor.importPresetJson (jsonObject, idx);
        });
    });
}

