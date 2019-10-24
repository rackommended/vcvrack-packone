#include "plugin.hpp"
#include <thread>

namespace Arena {

template < int IN_PORTS, int MIX_PORTS >
struct ArenaModule : Module {
	enum ParamIds {
		ENUMS(IN_X_POS, IN_PORTS),
		ENUMS(IN_Y_POS, IN_PORTS),
		ENUMS(IN_X_PARAM, IN_PORTS),
		ENUMS(IN_Y_PARAM, IN_PORTS),
		ENUMS(IN_X_CTRL, IN_PORTS),
		ENUMS(IN_Y_CTRL, IN_PORTS),
		ENUMS(IN_OP_PARAM, IN_PORTS),
		ENUMS(IN_PLUS_PARAM, IN_PORTS),
		ENUMS(IN_MINUS_PARAM, IN_PORTS),
		ENUMS(OUT_X_POS, MIX_PORTS),
		ENUMS(OUT_Y_POS, MIX_PORTS),
		ENUMS(OUT_SEL_PARAM, MIX_PORTS),
		NUM_PARAMS
	};
	enum InputIds {
		ENUMS(IN, IN_PORTS),
		ENUMS(IN_X_INPUT, IN_PORTS),
		ENUMS(IN_Y_INPUT, IN_PORTS),
		ENUMS(CTRL_INPUT, IN_PORTS),
		ENUMS(OUT_X_INPUT, MIX_PORTS),
		ENUMS(OUT_Y_INPUT, MIX_PORTS),
		NUM_INPUTS
	};
	enum OutputIds {
		ENUMS(OUT_MIX, MIX_PORTS),
		ENUMS(OUT, IN_PORTS),
		NUM_OUTPUTS
	};
	enum LightIds {
		ENUMS(IN_SEL_LIGHT, IN_PORTS),
		ENUMS(OUT_SEL_LIGHT, MIX_PORTS),
		NUM_LIGHTS
	};

	const int num_inports = IN_PORTS;
	const int num_outputs = MIX_PORTS;
	int selectedId = -1;
	int selectedType = -1;

	float radius[IN_PORTS];
	float dist[MIX_PORTS][IN_PORTS];

	dsp::SchmittTrigger outSelTrigger[MIX_PORTS];

	dsp::ClockDivider lightDivider;

	ArenaModule() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		// inputs
		for (int i = 0; i < IN_PORTS; i++) {
			configParam(IN_X_POS + i, 0.0f, 1.0f, 0.1f);
			configParam(IN_Y_POS + i, 0.0f, 1.0f, 0.1f + float(i) * (0.8f / (IN_PORTS - 1)));
		}
		// outputs
		for (int i = 0; i < MIX_PORTS; i++) {
			configParam(OUT_X_POS + i, 0.0f, 1.0f, 0.9f);
			configParam(OUT_Y_POS + i, 0.0f, 1.0f, 0.1f + float(i) * (0.8f / (MIX_PORTS - 1)));
		}
		onReset();
		lightDivider.setDivision(512);
	}

	void onReset() override {
		resetSelection();
		for (int i = 0; i < IN_PORTS; i++) {
			radius[i] = 0.5f;
			paramQuantities[IN_X_POS + i]->setValue(paramQuantities[IN_X_POS + i]->getDefaultValue());
			paramQuantities[IN_Y_POS + i]->setValue(paramQuantities[IN_Y_POS + i]->getDefaultValue());
		}
		for (int i = 0; i < MIX_PORTS; i++) {
			paramQuantities[OUT_X_POS + i]->setValue(paramQuantities[OUT_X_POS + i]->getDefaultValue());
			paramQuantities[OUT_Y_POS + i]->setValue(paramQuantities[OUT_Y_POS + i]->getDefaultValue());
		}
		Module::onReset();
	}

	void process(const ProcessArgs &args) override {
		float out[IN_PORTS];

		for (int i = 0; i < MIX_PORTS; i++) {
			if (inputs[OUT_X_INPUT + i].isConnected()) {
				float x = clamp(inputs[OUT_X_INPUT + i].getVoltage() / 10.f, 0.f, 1.f);
				params[OUT_X_POS + i].setValue(x);
			}
			if (inputs[OUT_Y_INPUT + i].isConnected()) {
				float y = clamp(inputs[OUT_Y_INPUT + i].getVoltage() / 10.f, 0.f, 1.f);
				params[OUT_Y_POS + i].setValue(y);
			}

			float x = params[OUT_X_POS + i].getValue();
			float y = params[OUT_Y_POS + i].getValue();
			Vec p = Vec(x, y);

			int c = 0;
			float mix = 0.f;
			for (int j = 0; j < IN_PORTS; j++) {
				if (inputs[IN_X_INPUT + i].isConnected()) {
					float x = clamp(inputs[IN_X_INPUT + i].getVoltage() / 10.f, 0.f, 1.f);
					params[IN_X_POS + i].setValue(x);
				}
				if (inputs[IN_Y_INPUT + i].isConnected()) {
					float y = clamp(inputs[IN_Y_INPUT + i].getVoltage() / 10.f, 0.f, 1.f);
					params[IN_Y_POS + i].setValue(y);
				}

				float in_x = params[IN_X_POS + j].getValue();
				float in_y = params[IN_Y_POS + j].getValue();
				Vec in_p = Vec(in_x, in_y);
				dist[i][j] = in_p.minus(p).norm();

				float r2 = radius[j];
				if (inputs[IN + j].isConnected() && dist[i][j] < r2) {
					float s = std::min(1.0f, (r2 - dist[i][j]) / r2 * 1.1f);
					s = clamp(inputs[IN + j].getVoltage(), 0.f, 10.f) * s;
					mix += s;
					out[j] += s;
					c++;
				}
			}

			if (c > 0) mix /= c;
			outputs[OUT_MIX + i].setVoltage(mix);

			for (int j = 0; j < IN_PORTS; j++)
				outputs[OUT + j].setVoltage(clamp(out[j], 0.f, 10.f));

			if (outSelTrigger[i].process(params[OUT_SEL_PARAM + i].getValue()))
				setSelection(1, i);
		}

		// Set lights infrequently
		if (lightDivider.process()) {
			for (int i = 0; i < IN_PORTS; i++) {
				lights[IN_SEL_LIGHT + i].setBrightness(selectedType == 0 && selectedId == i);
			}
			for (int i = 0; i < MIX_PORTS; i++) {
				lights[OUT_SEL_LIGHT + i].setBrightness(selectedType == 1 && selectedId == i);
			}
		}
	}

	inline void setSelection(int type, int id) {
		selectedType = type;
		selectedId = id;
	}

	inline bool isSelected(int type, int id) {
		return selectedType == type && selectedId == id;
	}

	inline void resetSelection() {
		selectedType = -1;
		selectedId = -1;
	}

	json_t* dataToJson() override {
		json_t* rootJ = json_object();
		json_t* radiusJ = json_array();
		for (int i = 0; i < IN_PORTS; i++) {
			json_t* d = json_real(radius[i]);
			json_array_append(radiusJ, d);
		}
		json_object_set_new(rootJ, "radius", radiusJ);
		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		json_t* radiusJ = json_object_get(rootJ, "radius");
		if (radiusJ) {
			json_t *d;
			size_t radiusIndex;
			json_array_foreach(radiusJ, radiusIndex, d) {
				radius[radiusIndex] = json_real_value(d);
			}
		}
	}
};


template < typename MODULE >
struct ArenaIoWidget : OpaqueWidget {
	const float KNOB_SENSITIVITY = 0.3f;
	const float radius = 9.f;
	const float fontsize = 13.0f;

	MODULE* module;
	std::shared_ptr<Font> font;
	ParamQuantity* paramQuantityX;
	ParamQuantity* paramQuantityY;
	int id = -1;
	int type = -1;
	
	NVGcolor color = nvgRGB(0x66, 0x66, 0x0);

	ArenaIoWidget() {
		font = APP->window->loadFont(asset::system("res/fonts/ShareTechMono-Regular.ttf"));
		box.size = Vec(2 * radius, 2 * radius);
	}

	void step() override {
		float posX = paramQuantityX->getValue() * (parent->box.size.x - box.size.x);
		box.pos.x = posX;
		float posY = paramQuantityY->getValue() * (parent->box.size.y - box.size.y);
		box.pos.y = posY;
	}

	void draw(const Widget::DrawArgs& args) override {
		Widget::draw(args);
		if (!module) return;

		Vec c = Vec(box.size.x / 2.f, box.size.y / 2.f);

		nvgGlobalCompositeOperation(args.vg, NVG_LIGHTER);

		if (module->isSelected(type, id)) {
			// selection halo
			float oradius = 1.8f * radius;
			NVGpaint paint;
			NVGcolor icol = color::mult(color::WHITE, 0.2f);
			NVGcolor ocol = nvgRGB(0, 0, 0);

			nvgBeginPath(args.vg);
			nvgCircle(args.vg, c.x, c.y, oradius);
			paint = nvgRadialGradient(args.vg, c.x, c.y, radius, oradius, icol, ocol);
			nvgFillPaint(args.vg, paint);
			nvgFill(args.vg);
		}

		// circle
		nvgBeginPath(args.vg);
		nvgCircle(args.vg, c.x, c.y, radius);
		nvgStrokeColor(args.vg, color);
		nvgStrokeWidth(args.vg, 1.0);
		nvgStroke(args.vg);
		nvgFillColor(args.vg, color::mult(color, 0.5f));
		nvgFill(args.vg);

		// label
		nvgFontSize(args.vg, fontsize);
		nvgFontFaceId(args.vg, font->handle);
		nvgFillColor(args.vg, color);
		nvgTextBox(args.vg, c.x - 3.f, c.y + 4.f, 120, string::f("%i", id + 1).c_str(), NULL);
	}

	void onHover(const event::Hover& e) override {
		math::Vec c = box.size.div(2);
		float dist = e.pos.minus(c).norm();
		if (dist <= c.x) {
			OpaqueWidget::onHover(e);
		}
	}

	void onButton(const event::Button& e) override {
		math::Vec c = box.size.div(2);
		float dist = e.pos.minus(c).norm();
		if (dist <= c.x) {
			OpaqueWidget::onButton(e);
			if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT) {
				module->setSelection(type, id);
				e.consume(this);
			}
			if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_RIGHT) {
				module->setSelection(type, id);
				createContextMenu();
				e.consume(this);
			}
		}
		else {
			OpaqueWidget::onButton(e);
		}
	}

	void onDragStart(const event::DragStart& e) override {
		if (e.button != GLFW_MOUSE_BUTTON_LEFT)
			return;

		APP->window->cursorLock();
	}

	void onDragEnd(const event::DragEnd& e) override {
		if (e.button != GLFW_MOUSE_BUTTON_LEFT)
			return;

		APP->window->cursorUnlock();
	}

	void onDragMove(const event::DragMove& e) override {
		float deltaX = e.mouseDelta.x / (parent->box.size.x - box.size.x);
		deltaX *= KNOB_SENSITIVITY;
		paramQuantityX->setValue(std::max(0.f, std::min(1.f, paramQuantityX->getValue() + deltaX)));

		float deltaY = e.mouseDelta.y / (parent->box.size.y - box.size.y);
		deltaY *= KNOB_SENSITIVITY;
		paramQuantityY->setValue(std::max(0.f, std::min(1.f, paramQuantityY->getValue() + deltaY)));

		OpaqueWidget::onDragMove(e);
	}

	virtual void createContextMenu() {}
};

template < typename MODULE >
struct ArenaInputWidget : ArenaIoWidget<MODULE> {
	typedef ArenaIoWidget<MODULE> AIOW;

	ArenaInputWidget() {
		AIOW::color = color::WHITE;
		AIOW::type = 0;
	}

	void draw(const Widget::DrawArgs& args) override {
		if (AIOW::module->isSelected(AIOW::type, AIOW::id)) {
			// outer circle and fill
			Vec c = Vec(AIOW::box.size.x / 2.f, AIOW::box.size.y / 2.f);
			Rect b = Rect(AIOW::box.pos.mult(-1), AIOW::parent->box.size);
			nvgSave(args.vg);
			nvgScissor(args.vg, b.pos.x, b.pos.y, b.size.x, b.size.y);
			float sizeX = AIOW::parent->box.size.x * AIOW::module->radius[AIOW::id] - 2.f * AIOW::radius;
			float sizeY = AIOW::parent->box.size.y * AIOW::module->radius[AIOW::id] - 2.f * AIOW::radius;
			nvgBeginPath(args.vg);
			nvgEllipse(args.vg, c.x, c.y, sizeX, sizeY);
			nvgGlobalCompositeOperation(args.vg, NVG_LIGHTER);
			nvgStrokeColor(args.vg, color::mult(AIOW::color, 0.8f));
			nvgStrokeWidth(args.vg, 1.0f);
			nvgStroke(args.vg);
			nvgFillColor(args.vg, color::mult(AIOW::color, 0.1f));
			nvgFill(args.vg);
			nvgResetScissor(args.vg);
			nvgRestore(args.vg);
		}

		AIOW::draw(args);
	}

	void createContextMenu() override {
		ui::Menu* menu = createMenu();
		menu->addChild(construct<MenuLabel>(&MenuLabel::text, string::f("Input %i", AIOW::id + 1).c_str()));

		struct RadiusQuantity : Quantity {
			MODULE* module;
			int id;

			RadiusQuantity(MODULE* module, int id) {
				this->module = module;
				this->id = id;
			}
			void setValue(float value) override {
				module->radius[id] = math::clamp(value, 0.f, 1.f);
			}
			float getValue() override {
				return module->radius[id];
			}
			float getDefaultValue() override {
				return 0.5;
			}
			float getDisplayValue() override {
				return getValue() * 100;
			}
			void setDisplayValue(float displayValue) override {
				setValue(displayValue / 100);
			}
			std::string getLabel() override {
				return "Radius";
			}
			std::string getUnit() override {
				return "";
			}
		};

		struct RadiusSlider : ui::Slider {
			RadiusSlider(MODULE* module, int id) {
				quantity = new RadiusQuantity(module, id);
			}
			~RadiusSlider() {
				delete quantity;
			}
		};

		RadiusSlider* radiusSlider = new RadiusSlider(AIOW::module, AIOW::id);
		radiusSlider->box.size.x = 200.0;
		menu->addChild(radiusSlider);
	}
};

template < typename MODULE >
struct ArenaOutputWidget : ArenaIoWidget<MODULE> {
	typedef ArenaIoWidget<MODULE> AIOW;

	ArenaOutputWidget() {
		AIOW::color = color::YELLOW;
		AIOW::type = 1;
	}

	void draw(const Widget::DrawArgs& args) override {
		AIOW::draw(args);

		Vec c = Vec(AIOW::box.size.x / 2.f, AIOW::box.size.y / 2.f);
		float sizeX = AIOW::parent->box.size.x;
		float sizeY = AIOW::parent->box.size.y;
		for (int i = 0; i < AIOW::module->num_inports; i++) {
			if (AIOW::module->dist[AIOW::id][i] < AIOW::module->radius[i]) {
				float x = AIOW::module->params[MODULE::IN_X_POS + i].getValue() * (sizeX - 2.f * AIOW::radius);
				float y = AIOW::module->params[MODULE::IN_Y_POS + i].getValue() * (sizeY - 2.f * AIOW::radius);
				Vec p = AIOW::box.pos.mult(-1).plus(Vec(x, y)).plus(c);
				Vec p_rad = p.minus(c).normalize().mult(AIOW::radius);
				Vec s = c.plus(p_rad);
				Vec t = p.minus(p_rad);
				nvgBeginPath(args.vg);
				nvgMoveTo(args.vg, s.x, s.y);
				nvgLineTo(args.vg, t.x, t.y);
				nvgStrokeColor(args.vg, color::mult(AIOW::color, 0.7f));
				nvgStrokeWidth(args.vg, 1.2f);
				nvgStroke(args.vg);
			}
		}
	}
};

struct LEDButton : app::SvgSwitch {
	LEDButton() {
		this->box.size = Vec(5.f, 5.f);
	}

	void draw(const Widget::DrawArgs& args) override {
		nvgBeginPath(args.vg);
		nvgCircle(args.vg, 2.5f, 2.5f, 2.0f);
		nvgStrokeColor(args.vg, color::mult(color::BLACK, 0.2f));
		nvgStrokeWidth(args.vg, 1.0f);
		nvgStroke(args.vg);
		app::SvgSwitch::draw(args);
	}
};

template < typename MODULE, int IN_PORTS, int MIX_PORTS >
struct ArenaAreaWidget : OpaqueWidget {
	MODULE* module;
	ArenaInputWidget<MODULE>* inwidget[IN_PORTS];
	ArenaOutputWidget<MODULE>* outwidget[MIX_PORTS];

	ArenaAreaWidget(MODULE* module, int inParamIdX, int inParamIdY, int outParamIdX, int outParamIdY) {
		this->module = module;
		if (module) {
			for (int i = 0; i < IN_PORTS; i++) {
				inwidget[i] = new ArenaInputWidget<MODULE>;
				inwidget[i]->module = module;
				inwidget[i]->paramQuantityX = module->paramQuantities[inParamIdX + i];
				inwidget[i]->paramQuantityY = module->paramQuantities[inParamIdY + i];
				inwidget[i]->id = i;
				addChild(inwidget[i]);
			}
			for (int i = 0; i < MIX_PORTS; i++) {
				outwidget[i] = new ArenaOutputWidget<MODULE>;
				outwidget[i]->module = module;
				outwidget[i]->paramQuantityX = module->paramQuantities[outParamIdX + i];
				outwidget[i]->paramQuantityY = module->paramQuantities[outParamIdY + i];
				outwidget[i]->id = i;
				addChild(outwidget[i]);
			}
		}
	}

	void onButton(const event::Button& e) override {
		if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT) {
			module->resetSelection();
		}
		OpaqueWidget::onButton(e);
		if (e.button == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_RIGHT && !e.isConsumed()) {
			createContextMenu();
			e.consume(this);
		}
	}

	void createContextMenu() {
		ui::Menu* menu = createMenu();
		menu->addChild(construct<MenuLabel>(&MenuLabel::text, "Menu"));
	}
};


struct ArenaWidget : ModuleWidget {
	typedef ArenaModule<8, 2> MODULE;
	MODULE* module;

	ArenaWidget(MODULE* module) {
		setModule(module);
		this->module = module;
		setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/Arena.svg")));

		addChild(createWidget<MyBlackScrew>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<MyBlackScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<MyBlackScrew>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<MyBlackScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		for (int i = 0; i < 4; i++) {
			float x = 25.9f + i * 30.433f;
			addInput(createInputCentered<StoermelderPort>(Vec(x, 65.6f), module, MODULE::IN + i));
			addInput(createInputCentered<StoermelderPort>(Vec(x, 96.4f), module, MODULE::IN_X_INPUT + i));
			addParam(createParamCentered<StoermelderTrimpot>(Vec(x, 120.4f), module, MODULE::IN_X_PARAM + i));
			addParam(createParamCentered<LEDButton>(Vec(x + 7.8f, 136.1f), module, MODULE::IN_X_POS + i));
			addChild(createLightCentered<SmallLight<BlueLight>>(Vec(x, 139.6f), module, MODULE::IN_SEL_LIGHT + i));
			addParam(createParamCentered<LEDButton>(Vec(x - 7.8f, 143.1f), module, MODULE::IN_Y_POS + i));
			addParam(createParamCentered<StoermelderTrimpot>(Vec(x, 158.8f), module, MODULE::IN_Y_PARAM + i));
			addInput(createInputCentered<StoermelderPort>(Vec(x, 182.8f), module, MODULE::IN_Y_INPUT + i));
			addOutput(createOutputCentered<StoermelderPort>(Vec(x, 323.4f), module, MODULE::OUT + i));
		}
		for (int i = 0; i < 4; i++) {
			float x = 497.6f + i * 30.433f;
			addInput(createInputCentered<StoermelderPort>(Vec(x, 65.6f), module, MODULE::IN + i + 4));
			addInput(createInputCentered<StoermelderPort>(Vec(x, 96.4f), module, MODULE::IN_X_INPUT + i + 4));
			addParam(createParamCentered<StoermelderTrimpot>(Vec(x , 120.4f), module, MODULE::IN_X_PARAM + i + 4));
			addParam(createParamCentered<LEDButton>(Vec(x - 7.8f, 136.1f), module, MODULE::IN_X_POS + i + 4));
			addChild(createLightCentered<SmallLight<BlueLight>>(Vec(x, 139.6f), module, MODULE::IN_SEL_LIGHT + i + 4));
			addParam(createParamCentered<LEDButton>(Vec(x + 7.8f, 143.1f), module, MODULE::IN_Y_POS + i + 4));
			addParam(createParamCentered<StoermelderTrimpot>(Vec(x, 158.8f), module, MODULE::IN_Y_PARAM + i + 4));
			addInput(createInputCentered<StoermelderPort>(Vec(x, 182.8f), module, MODULE::IN_Y_INPUT + i + 4));
			addOutput(createOutputCentered<StoermelderPort>(Vec(x, 323.4f), module, MODULE::OUT + i + 4));
		}

		ArenaAreaWidget<MODULE, 8, 2>* area = new ArenaAreaWidget<MODULE, 8, 2>(module, MODULE::IN_X_POS, MODULE::IN_Y_POS, MODULE::OUT_X_POS, MODULE::OUT_Y_POS);
		area->box.pos = Vec(166.f, 53.3f);
		area->box.size = Vec(283.f, 237.7f);
		addChild(area);

		addOutput(createOutputCentered<StoermelderPort>(Vec(195.5f, 323.4f), module, MODULE::OUT_MIX + 0));
		addInput(createInputCentered<StoermelderPort>(Vec(238.1f, 323.4f), module, MODULE::OUT_X_INPUT + 0));
		addParam(createParamCentered<LEDButton>(Vec(255.1f, 326.9f), module, MODULE::OUT_X_POS + 0));
		addChild(createLightCentered<SmallLight<BlueLight>>(Vec(262.7f, 323.4f), module, MODULE::OUT_SEL_LIGHT + 0));
		addParam(createParamCentered<LEDButton>(Vec(270.3f, 319.9f), module, MODULE::OUT_X_POS + 0));
		addInput(createInputCentered<StoermelderPort>(Vec(287.3f, 323.4f), module, MODULE::OUT_Y_INPUT + 0));
		addInput(createInputCentered<StoermelderPort>(Vec(327.7f, 323.4f), module, MODULE::OUT_X_INPUT + 1));
		addParam(createParamCentered<LEDButton>(Vec(344.7f, 319.9f), module, MODULE::OUT_X_POS + 1));
		addChild(createLightCentered<SmallLight<BlueLight>>(Vec(352.3f, 323.4f), module, MODULE::OUT_SEL_LIGHT + 1));
		addParam(createParamCentered<LEDButton>(Vec(359.8f, 326.9f), module, MODULE::OUT_Y_POS + 1));
		addInput(createInputCentered<StoermelderPort>(Vec(376.9f, 323.4f), module, MODULE::OUT_Y_INPUT + 1));
		addOutput(createOutputCentered<StoermelderPort>(Vec(419.4f, 323.4f), module, MODULE::OUT_MIX + 1));

		//addParam(createParamCentered<TL1105>(Vec(265.3f, 326.9f), module, MODULE::OUT_SEL_PARAM + 0));
		//addParam(createParamCentered<TL1105>(Vec(349.7f, 326.9f), module, MODULE::OUT_SEL_PARAM + 1));
	}

	void appendContextMenu(Menu* menu) override {
		struct ManualItem : MenuItem {
			void onAction(const event::Action &e) override {
				std::thread t(system::openBrowser, "https://github.com/stoermelder/vcvrack-packone/blob/v1/docs/Arena.md");
				t.detach();
			}
		};

		menu->addChild(construct<ManualItem>(&MenuItem::text, "Module Manual"));
		menu->addChild(new MenuSeparator());
	}
};

} // namespace Arena

Model* modelArena = createModel<Arena::ArenaModule<8, 2>, Arena::ArenaWidget>("Arena");