#include <grend/gameMain.hpp>
#include <grend/gameMainDevWindow.hpp>
#include <grend/gameObject.hpp>
//#include <grend/playerView.hpp>
#include <grend/geometryGeneration.hpp>
#include <grend/gameEditor.hpp>
#include <grend/controllers.hpp>

#include <grend/ecs/ecs.hpp>
#include <grend/ecs/rigidBody.hpp>
#include <grend/ecs/collision.hpp>
#include <grend/ecs/serializer.hpp>

#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <sys/types.h>

#include <memory>
#include <chrono>
#include <map>
#include <vector>
#include <set>

#include <initializer_list>

// XXX:  toggle using textures/models I have locally, don't want to
//       bloat the assets folder again
#define LOCAL_BUILD 0

using namespace grendx;
using namespace grendx::ecs;

#include "player.hpp"
#include "enemy.hpp"
#include "inputHandler.hpp"
#include "landscapeGenerator.hpp"
#include "projectile.hpp"
#include "health.hpp"
#include "healthbar.hpp"
#include "enemyCollision.hpp"
#include "healthPickup.hpp"
#include "timedLifetime.hpp"

class landscapeEventSystem : public entitySystem {
	public:
		virtual void update(entityManager *manager, float delta);

		generatorEventQueue::ptr queue = std::make_shared<generatorEventQueue>();
};

class generatorEventHandler : public component {
	public:
		generatorEventHandler(entityManager *manager, entity *ent)
			: component(manager, ent)
		{
			manager->registerComponent(ent, "generatorEventHandler", this);
		}

		virtual void
		handleEvent(entityManager *manager, entity *ent, generatorEvent& ev) {
			const char *typestr;

			switch (ev.type) {
				case generatorEvent::types::generatorStarted:
					typestr =  "started";
					break;

				case generatorEvent::types::generated:
					typestr =  "generated";
					break;

				case generatorEvent::types::deleted:
					typestr =  "deleted";
					break;

				default:
					typestr =  "<unknown>";
					break;
			}

			SDL_Log(
				"handleEvent: got here, %s [+/-%g] [+/-%g] [+/-%g]",
				typestr, ev.extent.x, ev.extent.y, ev.extent.z);

			/*

			std::cerr << std::endl;
			*/
		}
};

class generatorEventActivator : public generatorEventHandler {
	public:
		generatorEventActivator(entityManager *manager, entity *ent)
			: generatorEventHandler(manager, ent)
		{
			manager->registerComponent(ent, "generatorEventActivator", this);
		}

		virtual void
		handleEvent(entityManager *manager, entity *ent, generatorEvent& ev) {
			// TODO: activate/deactivate stuff here
		}
};

void landscapeEventSystem::update(entityManager *manager, float delta) {
	auto handlers = manager->getComponents("generatorEventHandler");
	auto g = queue->lock();
	auto& quevec = queue->getQueue();

	for (auto& ev : quevec) {
		for (auto& it : handlers) {
			generatorEventHandler *handler = dynamic_cast<generatorEventHandler*>(it);
			entity *ent = manager->getEntity(handler);

			if (handler && ent) {
				handler->handleEvent(manager, ent, ev);
			}
		}
	}

	// XXX: should be in queue class
	quevec.clear();
}

// XXX: this sort of makes sense but not really... game entity with no renderable
//      objects, functioning as basically a subsystem? 
//      abstraction here doesn't make sense, needs to be redone
class worldEntityGenerator : public generatorEventHandler {
	public:
		worldEntityGenerator(entityManager *manager, entity *ent)
			: generatorEventHandler(manager, ent)
		{
			manager->registerComponent(ent, "generatorEventHandler", this);
		}

		virtual void
		handleEvent(entityManager *manager, entity *ent, generatorEvent& ev) {
			switch (ev.type) {
				case generatorEvent::types::generated:
					{
						// XXX
						std::tuple<float, float, float> foo =
							{ ev.position.x, ev.position.y, ev.position.z };

						if (positions.count(foo) == 0) {
							positions.insert(foo);
							SDL_Log("worldEntityGenerator(): generating some things");

							//manager->add(new enemy(manager, manager->engine, ev.position + glm::vec3(0, 50.f, 0)));

							// TODO: need a way to know what the general shape of
							//       the generated thing is...
							//manager->add(new healthPickup(manager, ev.position + glm::vec3(0, 10.f, 0)));
						}
					}
					break;

				case generatorEvent::types::deleted:
					break;

				default:
					break;
			}
		}

		std::set<std::tuple<float, float, float>> positions;
};

class worldEntitySpawner : public entity {
	public:
		worldEntitySpawner(entityManager *manager)
			: entity (manager)
		{
			manager->registerComponent(this, "worldEntitySpawner", this);
			new worldEntityGenerator(manager, this);
		}

		virtual void update(entityManager *manager, float delta) { /* nop */ };
};

class landscapeGenView : public gameView {
	public:
		typedef std::shared_ptr<landscapeGenView> ptr;
		typedef std::weak_ptr<landscapeGenView>   weakptr;

		landscapeGenView(gameMain *game);
		virtual void logic(gameMain *game, float delta);
		virtual void render(gameMain *game);
		//void loadPlayer(void);

		enum modes {
			MainMenu,
			Move,
			Pause,
		};

		renderPostChain::ptr post = nullptr;
		//modalSDLInput input;
		vecGUI vgui;
		int menuSelect = 0;
		float zoom = 10.f;

		landscapeGenerator landscape;
		inputHandlerSystem::ptr inputSystem;
};

// XXX
static glm::vec2 movepos(0, 0);
static glm::vec2 actionpos(0, 0);

landscapeGenView::landscapeGenView(gameMain *game) : gameView() {
	post = renderPostChain::ptr(new renderPostChain(
				{loadPostShader(GR_PREFIX "shaders/src/texpresent.frag", game->rend->globalShaderOptions)},
				//{game->rend->postShaders["tonemap"], game->rend->postShaders["psaa"]},
				SCREEN_SIZE_X, SCREEN_SIZE_Y));

	// TODO: names are kinda pointless here
	// TODO: should systems be a state object in gameMain as well?
	//       they practically are since the entityManager here is, just one
	//       level deep...
	game->entities->systems["lifetime"] = std::make_shared<lifetimeSystem>();
	game->entities->systems["collision"] = std::make_shared<entitySystemCollision>();
	game->entities->systems["syncPhysics"] = std::make_shared<syncRigidBodySystem>();

	inputSystem = std::make_shared<inputHandlerSystem>();
	game->entities->systems["input"] = inputSystem;

	auto generatorSys = std::make_shared<landscapeEventSystem>();
	game->entities->systems["landscapeEvents"] = generatorSys;
	landscape.setEventQueue(generatorSys->queue);

	//manager->add(new player(manager.get(), game, glm::vec3(-15, 50, 0)));
	/*
	player *playerEnt = new player(game->entities.get(), game, glm::vec3(0, 20, 0));
	game->entities->add(playerEnt);
	new generatorEventHandler(game->entities.get(), playerEnt);
	new health(game->entities.get(), playerEnt);
	new enemyCollision(game->entities.get(), playerEnt);
	new healthPickupCollision(game->entities.get(), playerEnt);
	new mouseRotationPoller(game->entities.get(), playerEnt);

	game->entities->add(new worldEntitySpawner(game->entities.get()));
	*/

	for (unsigned i = 0; i < 10; i++) {
		glm::vec3 position = glm::vec3(
			float(rand()) / RAND_MAX * 100.0 - 50,
			50.0,
			float(rand()) / RAND_MAX * 100.0 - 50
		);

		game->entities->add(new enemy(game->entities.get(), game, position));
	}

	bindCookedMeshes();
	input.bind(MODAL_ALL_MODES, resizeInputHandler(game, post));

#if !defined(__ANDROID__)
	input.bind(modes::Move, controller::camMovement(cam, 10.f));
	//input.bind(modes::Move, controller::camFPS(cam, game));
	input.bind(modes::Move, controller::camScrollZoom(cam, &zoom));
	input.bind(modes::Move, inputMapper(inputSystem->inputs, cam));
#endif

	input.bind(modes::Move, controller::camAngled2DFixed(cam, game, -M_PI/4.f));
	input.bind(modes::Move, [=] (SDL_Event& ev, unsigned flags) {
		inputSystem->handleEvent(game->entities.get(), ev);
		return MODAL_NO_CHANGE;
	});

	input.bind(MODAL_ALL_MODES,
		[&, this] (SDL_Event& ev, unsigned flags) {
			if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_h) {
				// TODO: log interface
				nlohmann::json compJson;

				for (auto& ent : game->entities->entities) {
					//std::cerr << ent->typeString() << std::endl;
					if (game->factories->has(ent->typeString())) {
						compJson.push_back(ent->serialize(game->entities.get()));
					}
				}

				std::cerr << compJson.dump(4) << std::endl;
			}

			return MODAL_NO_CHANGE;
		});

	input.setMode(modes::Move);
};

void landscapeGenView::logic(gameMain *game, float delta) {
	//SDL_Log("Got to landscapeGenView::logic()");
	static glm::vec3 lastvel = glm::vec3(0);
	static gameObject::ptr retval;

	if (cam->velocity() != lastvel) {
		lastvel = cam->velocity();
	}

	game->phys->stepSimulation(delta);
	game->phys->filterCollisions();;
	
	entity *playerEnt = findFirst(game->entities.get(), {"player"});

	if (!playerEnt) {
		playerEnt = new player(game->entities.get(), game, glm::vec3(-5, 20, -5));
		game->entities->add(playerEnt);
		new generatorEventHandler(game->entities.get(), playerEnt);
		new health(game->entities.get(), playerEnt);
		new enemyCollision(game->entities.get(), playerEnt);
		new healthPickupCollision(game->entities.get(), playerEnt);

#if defined(__ANDROID__)
		int wx = game->rend->screen_x;
		int wy = game->rend->screen_y;
		glm::vec2 movepad  ( 2*wx/16.f, 7*wy/9.f);
		glm::vec2 actionpad(14*wx/16.f, 7*wy/9.f);

		new touchMovementHandler(game->entities.get(), playerEnt, cam,
		                         inputSystem->inputs, movepad, 150.f);
		new touchRotationHandler(game->entities.get(), playerEnt, cam,
		                         inputSystem->inputs, actionpad, 150.f);
		
#else
		new mouseRotationPoller(game->entities.get(), playerEnt);
#endif
	}

	if (playerEnt) {
		TRS& transform = playerEnt->getNode()->transform;
		cam->setPosition(transform.position - zoom*cam->direction());
		landscape.setPosition(game, transform.position);
	}

	game->entities->update(delta);
}

static void drawPlayerHealthbar(entityManager *manager,
                                vecGUI&vgui,
                                health *playerHealth)
{
	int wx = manager->engine->rend->screen_x;
	int wy = manager->engine->rend->screen_y;

	nvgBeginPath(vgui.nvg);
	nvgRoundedRect(vgui.nvg, 48, 35, 16, 42, 3);
	nvgRoundedRect(vgui.nvg, 35, 48, 42, 16, 3);
	nvgFillColor(vgui.nvg, nvgRGBA(172, 16, 16, 192));
	nvgFill(vgui.nvg);

	//nvgRotate(vgui.nvg, 0.1*cos(ticks));
	nvgBeginPath(vgui.nvg);
	nvgRect(vgui.nvg, 90, 44, 256, 24);
	nvgFillColor(vgui.nvg, nvgRGBA(30, 30, 30, 127));
	nvgFill(vgui.nvg);

	nvgBeginPath(vgui.nvg);
	nvgRect(vgui.nvg, 93, 47, 252*playerHealth->amount, 20);
	nvgFillColor(vgui.nvg, nvgRGBA(192, 32, 32, 127));
	nvgFill(vgui.nvg);
	//nvgRotate(vgui.nvg, -0.1*cos(ticks));

	nvgBeginPath(vgui.nvg);
	nvgRoundedRect(vgui.nvg, wx - 250, 50, 200, 100, 10);
	nvgFillColor(vgui.nvg, nvgRGBA(28, 30, 34, 192));
	nvgFill(vgui.nvg);

	nvgFontSize(vgui.nvg, 16.f);
	nvgFontFace(vgui.nvg, "sans-bold");
	nvgFontBlur(vgui.nvg, 0);
	nvgTextAlign(vgui.nvg, NVG_ALIGN_LEFT);
	nvgFillColor(vgui.nvg, nvgRGBA(0xf0, 0x60, 0x60, 160));
	nvgText(vgui.nvg, wx - 82, 80, "❎", NULL);
	nvgFillColor(vgui.nvg, nvgRGBA(220, 220, 220, 160));
	nvgText(vgui.nvg, wx - 235, 80, "💚 Testing this", NULL);
	nvgText(vgui.nvg, wx - 235, 80 + 16, "Go forward ➡", NULL);
	nvgText(vgui.nvg, wx - 235, 80 + 32, "⬅ Go back", NULL);

	double fps = manager->engine->frame_timer.average();
	std::string fpsstr = std::to_string(fps) + "fps";
	nvgFillColor(vgui.nvg, nvgRGBA(0xf0, 0x60, 0x60, 0xff));
	nvgText(vgui.nvg, wx/2, 80 + 32, fpsstr.c_str(), NULL);
}

static void renderHealthbars(entityManager *manager,
                             vecGUI& vgui,
                             camera::ptr cam)
{
	std::set<entity*> ents = searchEntities(manager, {"health", "healthbar"});
	std::set<entity*> players = searchEntities(manager, {"player", "health"});

	for (auto& ent : ents) {
		healthbar *bar;
		castEntityComponent(bar, manager, ent, "healthbar");

		if (bar) {
			bar->draw(manager, ent, vgui, cam);
		}
	}

	if (players.size() > 0) {
		entity *ent = *players.begin();
		health *playerHealth;

		castEntityComponent(playerHealth, manager, ent, "health");

		if (playerHealth) {
			drawPlayerHealthbar(manager, vgui, playerHealth);
		}
	}
}

static void renderControls(gameMain *game, vecGUI& vgui) {
	// TODO: should have a generic "pad" component
	// assume first instances of these components are the ones we want,
	// since there should only be 1 of each
	auto& movepads   = game->entities->getComponents("touchMovementHandler");
	auto& actionpads = game->entities->getComponents("touchRotationHandler");
	touchMovementHandler *movepad;
	touchRotationHandler *actionpad;

	if (movepads.size() == 0 || actionpads.size() == 0) {
		return;
	}

	movepad   = dynamic_cast<touchMovementHandler*>(*movepads.begin());
	actionpad = dynamic_cast<touchRotationHandler*>(*actionpads.begin());

	// left movement pad
	nvgStrokeWidth(vgui.nvg, 2.0);
	nvgBeginPath(vgui.nvg);
	nvgCircle(vgui.nvg, movepad->center.x + movepad->touchpos.x,
	                    movepad->center.y + movepad->touchpos.y,
	                    movepad->radius / 3.f);
	nvgFillColor(vgui.nvg, nvgRGBA(0x60, 0x60, 0x60, 0x80));
	nvgStrokeColor(vgui.nvg, nvgRGBA(255, 255, 255, 192));
	nvgStroke(vgui.nvg);
	nvgFill(vgui.nvg);

	nvgStrokeWidth(vgui.nvg, 2.0);
	nvgBeginPath(vgui.nvg);
	nvgCircle(vgui.nvg, movepad->center.x, movepad->center.y, movepad->radius);
	nvgStrokeColor(vgui.nvg, nvgRGBA(0x60, 0x60, 0x60, 0x40));
	nvgStroke(vgui.nvg);

	// right action pad
	nvgStrokeWidth(vgui.nvg, 2.0);
	nvgBeginPath(vgui.nvg);
	nvgCircle(vgui.nvg, actionpad->center.x + actionpad->touchpos.x,
	                    actionpad->center.y + actionpad->touchpos.y,
	                    actionpad->radius / 3.f);
	nvgFillColor(vgui.nvg, nvgRGBA(0x60, 0x60, 0x60, 0x80));
	nvgStrokeColor(vgui.nvg, nvgRGBA(255, 255, 255, 192));
	nvgStroke(vgui.nvg);
	nvgFill(vgui.nvg);

	nvgStrokeWidth(vgui.nvg, 2.0);
	nvgBeginPath(vgui.nvg);
	nvgCircle(vgui.nvg, actionpad->center.x, actionpad->center.y, actionpad->radius);
	nvgStrokeColor(vgui.nvg, nvgRGBA(0x60, 0x60, 0x60, 0x40));
	nvgStroke(vgui.nvg);
}

void landscapeGenView::render(gameMain *game) {
	//SDL_Log("Got to landscapeGenView::render()");
	int winsize_x, winsize_y;
	SDL_GetWindowSize(game->ctx.window, &winsize_x, &winsize_y);
	renderFlags flags = game->rend->getLightingFlags();

	if (input.mode == modes::MainMenu) {
		renderWorld(game, cam, flags);

		// TODO: need to set post size on resize event..
		//post->setSize(winsize_x, winsize_y);
		post->draw(game->rend->framebuffer);
		input.setMode(modes::Move);

		// TODO: function to do this
		//drawMainMenu(winsize_x, winsize_y);

	} else {
		renderWorld(game, cam, flags);
		post->draw(game->rend->framebuffer);

		Framebuffer().bind();
		setDefaultGlFlags();

		disable(GL_DEPTH_TEST);
		disable(GL_SCISSOR_TEST);
		glClear(GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

		nvgBeginFrame(vgui.nvg, game->rend->screen_x, game->rend->screen_y, 1.0);
		nvgSave(vgui.nvg);

		renderHealthbars(game->entities.get(), vgui, cam);
		renderControls(game, vgui);

		nvgRestore(vgui.nvg);
		nvgEndFrame(vgui.nvg);
	}
}

#if defined(_WIN32)
extern "C" {
int WinMain(int argc, char *argv[]);
}

int WinMain(int argc, char *argv[]) {
#else
int main(int argc, char *argv[]) {
#endif
	/*
	std::cerr << "entering main()" << std::endl;
	std::cerr << "started SDL context" << std::endl;
	std::cerr << "have game state" << std::endl;
	*/
	SDL_Log("entering main()");
	SDL_Log("started SDL context");
	SDL_Log("have game state");

	try {
		TRS staticPosition; // default
		gameMain *game = new gameMainDevWindow();
		//gameMain *game = new gameMain();

		// TODO: better way to do this
#define SERIALIZABLE(T) game->factories->add<T>()
		SERIALIZABLE(entity);
		SERIALIZABLE(component);

		SERIALIZABLE(rigidBody);
		SERIALIZABLE(rigidBodySphere);
		SERIALIZABLE(rigidBodyBox);
		SERIALIZABLE(syncRigidBodyTransform);
		SERIALIZABLE(syncRigidBodyPosition);
		SERIALIZABLE(syncRigidBodyXZVelocity);
		//SERIALIZABLE(collisionHandler);
#undef SERIALIZABLE

	// XXX:  toggle using textures I have locally, don't want to bloat the assets
	//       folder again
	// TODO: include some default textures in assets for this, or procedurally generate
#if LOCAL_BUILD
		landscapeMaterial = std::make_shared<material>();
		landscapeMaterial->factors.diffuse = {1, 1, 1, 1};
		landscapeMaterial->factors.ambient = {1, 1, 1, 1};
		landscapeMaterial->factors.specular = {1, 1, 1, 1};
		landscapeMaterial->factors.emissive = {1, 1, 1, 1};
		landscapeMaterial->factors.roughness = 1.f;
		landscapeMaterial->factors.metalness = 1.f;
		landscapeMaterial->factors.opacity = 1;
		landscapeMaterial->factors.refract_idx = 1.5;

		landscapeMaterial->maps.diffuse
			= std::make_shared<materialTexture>("/home/flux/blender/tex/aerial_grass_rock_2k_jpg/aerial_grass_rock_diff_2k.jpg");
		landscapeMaterial->maps.metalRoughness
			= std::make_shared<materialTexture>("/home/flux/blender/tex/aerial_grass_rock_2k_jpg/aerial_grass_rock_rough_green_2k.jpg");
		landscapeMaterial->maps.normal
			= std::make_shared<materialTexture>("/home/flux/blender/tex/aerial_grass_rock_2k_jpg/aerial_grass_rock_nor_2k.jpg");
		landscapeMaterial->maps.ambientOcclusion
			= std::make_shared<materialTexture>("/home/flux/blender/tex/aerial_grass_rock_2k_jpg/aerial_grass_rock_ao_2k.jpg");
		landscapeMaterial->maps.emissive
			= std::make_shared<materialTexture>(GR_PREFIX "assets/tex/black.png");

		treeNode = load_object("assets-old/obj/Modular Terrain Hilly/Prop_Tree_Pine_3.obj");

#else
		landscapeMaterial = std::make_shared<material>();
		landscapeMaterial->factors.diffuse = {0.15, 0.3, 0.1, 1};
		landscapeMaterial->factors.ambient = {1, 1, 1, 1};
		landscapeMaterial->factors.specular = {1, 1, 1, 1};
		landscapeMaterial->factors.emissive = {0, 0, 0, 0};
		landscapeMaterial->factors.roughness = 0.9f;
		landscapeMaterial->factors.metalness = 0.f;
		landscapeMaterial->factors.opacity = 1;
		landscapeMaterial->factors.refract_idx = 1.5;

		treeNode = generate_cuboid(1.0, 1.0, 1.0);
#endif

		compileModel("treedude", treeNode);

		game->jobs->addAsync([=] {
			auto foo = openSpatialLoop(GR_PREFIX "assets/sfx/Bit Bit Loop.ogg");
			foo->worldPosition = glm::vec3(-10, 0, -5);
			game->audio->add(foo);
			return true;
		});

		game->jobs->addAsync([=] {
			auto bar = openSpatialLoop(GR_PREFIX "assets/sfx/Meditating Beat.ogg");
			bar->worldPosition = glm::vec3(0, 0, -5);
			game->audio->add(bar);
			return true;
		});

		game->state->rootnode = loadMap(game);
		game->phys->addStaticModels(nullptr, game->state->rootnode, staticPosition);

		landscapeGenView::ptr player = std::make_shared<landscapeGenView>(game);
		player->landscape.setPosition(game, glm::vec3(1));
		player->cam->setFar(1000.0);
		game->setView(player);
		game->rend->lightThreshold = 0.5;
		//ENABLE_GL_ERROR_CHECK(false);

		gameLightDirectional::ptr dlit = std::make_shared<gameLightDirectional>();

		setNode("entities",  game->state->rootnode, game->entities->root);
		setNode("landscape", game->state->rootnode, player->landscape.getNode());
		//setNode("testlight", game->state->rootnode, dlit);

		SDL_Log("Got to game->run()!");
		game->run();

	} catch (const std::exception& ex) {
		SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Exception! %s", ex.what());

	} catch (const char* ex) {
		SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Exception! %s", ex);
	}

	return 0;
}
