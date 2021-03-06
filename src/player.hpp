#pragma once

#include <grend/gameObject.hpp>
#include <grend/animation.hpp>
#include <grend/ecs/ecs.hpp>
#include <grend/ecs/rigidBody.hpp>

#include <memory>
#include <vector>

#include "boxSpawner.hpp"
#include "inputHandler.hpp"

using namespace grendx;
using namespace grendx::ecs;

animationCollection::ptr findAnimations(gameObject::ptr obj);

class animatedCharacter {
	public:
		typedef std::shared_ptr<animatedCharacter> ptr;
		typedef std::weak_ptr<animatedCharacter>   weakptr;

		animatedCharacter(gameObject::ptr objs);
		void setAnimation(std::string animation, float weight = 1.0);
		gameObject::ptr getObject(void);

	private:
		animationCollection::ptr animations;
		std::string currentAnimation;
		gameObject::ptr objects;
};

class player : public entity {
	public:
		player(entityManager *manager, gameMain *game, glm::vec3 position);
		virtual void update(entityManager *manager, float delta);
		virtual gameObject::ptr getNode(void) { return node; };

		animatedCharacter::ptr character;
		rigidBody *body;
};

