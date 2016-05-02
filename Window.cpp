#include <iostream>
using namespace std;
#include <Windows.h>

#include "glee.h"
#ifdef __APPLE__
    #include <GLUT/glut.h>
#else
    #include <GL/glut.h>
#endif
#include "Window.h"

/********************* Globals *********************/
int Window::width  = 1024;	//Set window width in pixels
int Window::height = 768;	//Set window height in pixels

Vector3 lastPoint;

bool onCooldown = false;
bool leftClick = false;
bool leftPressed = false;
bool rightPressed = false;
bool spacePressed = false;
bool initRun = true;

int cooldownTimer = 0;
int frameCount = 0;
int lifeTracker = 5;
int myScore = 0;
int elapsedTime = 0;

Color white(0xffffffff);
Color red(0xff0000ff);

string gameOver = "Game Over";
string instruct = "Press Space to Continue";
string score = "Score: ";

Background background(0);

Matrix4 missles[MAX];
int mPositions[MAX];	//Missle positions

Matrix4 enemies[MAX];
int ePositions[MAX];	//Enemy Positions
int givesLife[MAX];		//Marks special life giving enemies

Matrix4 explosions[MAX];
int exPositions[MAX];	//Explosion positions

ParticleEmitter explosion;
ParticleEffect explosionEffect[MAX];
ParticleEffect::ColorInterpolator colors;
/***************************************************/

void Window::initialize(void)
{
	//Reset all globals//
	onCooldown = false;
	leftClick = false;
	leftPressed = false;
	rightPressed = false;
	spacePressed = false;

	cooldownTimer = 0;
	frameCount = 0;
	lifeTracker = 5;
	myScore = 0;

	for(int i = 0; i < MAX; i++)
	{
		mPositions[i] = 0;
		ePositions[i] = 0;
		exPositions[i] = 0;
		givesLife[i] = 0;
	}
	/////////////////////

    //Set lighting
    Vector4 lightPos(0.0, 0.0, 50.0, 0.0);	//Centered, behind camera, directional
    Globals::light.position = lightPos;

	//Initialize ship
	if(initRun)	//Only on initial application run
	{
		string filename = "data/models/dark_fighter_6.obj";
		Globals::ship.parse(filename);

		filename = "data/models/space_frigate_6.obj";
		Globals::enemy.parse(filename);
	}

	//Place at origin
	Globals::ship.toWorld.identity();

	//Rotate to nose-up position
	Matrix4 rotate;
	rotate.makeRotateX(PI/2);
	Globals::ship.toWorld = rotate * Globals::ship.toWorld;

	if(initRun) //Only on initial application run
	{
		//Set background
		background = Background(400.0);
		Globals::bg = Texture("data/models/purplenebula_ft.ppm");	//Front face

		//Set color interpolator
		colors.AddValue(0.0f, glm::vec4(1, 1, 1, 1));		//white	
		colors.AddValue(0.35f, glm::vec4(1, 1, 0, 1));		//yellow
		colors.AddValue(0.45f, glm::vec4(1, 0.45, 0, 1));	//orange

		//Set particle effect
		explosion.MinimumRadius = 0;
		explosion.MaximumRadius = 0;
		explosion.MinInclination = 0;
		explosion.MaxInclination = 360;
		explosion.MinAzimuth = 0;
		explosion.MaxAzimuth = 360;
		explosion.MinSpeed = 0;
		explosion.MaxSpeed = 2;
		explosion.MinLifetime = 70;
		explosion.MaxLifetime = 70;
		
		//Initialize all particle instances
		//Don't render until hit detected
		for(int i = 0; i < MAX; i++)
		{
			explosionEffect[i] = ParticleEffect(100);
			explosionEffect[i].toWorld.identity();
			explosionEffect[i].SetColorInterplator(colors);
			explosionEffect[i].SetParticleEmitter(&explosion);
		}
	}

	//Initial run complete
	initRun = false;
}

void Window::idleCallback()
{
	updateTimers();

	processKeyFuncs();

	hitDetector();

	//Static time delta for update calls
    Globals::updateData.dt = 1.0/60.0;	//60 fps

	displayCallback();
}

void Window::reshapeCallback(int w, int h)
{
    width = w;								//Set the window width
    height = h;								//Set the window height
    glViewport(0, 0, w, h);					//Set new viewport size
	glMatrixMode(GL_PROJECTION);			//Set the OpenGL matrix mode to Projection
    glLoadIdentity();						//Clear the projection matrix by loading the identity
	glOrtho(-256, 256, -256, 256, 1, 100);	//Set orthographic projection
	//NOTE:   ^?
}

void Window::displayCallback()
{
    //Clear color and depth buffers
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    
    //Set matrix mode to ModelView
    glMatrixMode(GL_MODELVIEW);
    
    //Convert world coordiantes into camera coordiantes
    glLoadMatrixf(Globals::camera.getInverseMatrix().ptr());
     
	drawHUD();

	//Push a matrix save point
    glPushMatrix();

    Globals::light.bind(0);

	if(lifeTracker == 0)
		gameOverScreen();

	else
	{
		//NOTE: BG not working
		//background.draw_bg(Globals::drawData);

		Globals::ship.draw(Globals::drawData);

		for(int i = 0; i < MAX; i++)
			if(exPositions[i])
				explosionEffect[i].Render();

		glColor3f(1, 1, 1);
		animateMissles();
		animateEnemies();
	}

    //Pop off changes we made to matrix stack this frame
    glPopMatrix();
    
    //Clear any outstanding commands in command buffer
    glFlush();
    
    //Swap off-screen buffer with on-screen buffer
    glutSwapBuffers();
}

//Flag when keys are pressed
void Window::normalKeys(unsigned char key, int x, int y)
{
	if(key == ' ')
		spacePressed = true;

	if(key == 'q')   //Quit application
		exit(0);
}

//Flag when keys are unpressed
void Window::normalKeysUp(unsigned char key, int x, int y)
{
	if(key == ' ')
		spacePressed = false;
}

//Flag when special keys are pressed
void Window::specialKeys(int key, int x, int y)
{
	if(GLUT_KEY_LEFT == key)
		leftPressed = true;

	if(GLUT_KEY_RIGHT == key)
		rightPressed = true;
}

//Flag when special keys are unpressed
void Window::specialKeysUp(int key, int x, int y)
{
	if(GLUT_KEY_LEFT == key)
		leftPressed = false;

	if(GLUT_KEY_RIGHT == key)
		rightPressed = false;
}

//Process functions for all pressed keys
void Window::processKeyFuncs()
{
	if(leftPressed || rightPressed)
		moveShip();

	if(spacePressed)
	{
		//Restart if game over
		if(lifeTracker == 0)
			initialize();

		else
			fireMissle();
	}
}

void Window::fireMissle()
{
	//Do not fire if on cooldown
	if(!onCooldown)
	{
		onCooldown = true;

		int position = seekPosition(mPositions);

		if(position < MAX)
			missles[position] = Globals::ship.toWorld;
	}
}

void Window::moveShip()
{
	Matrix4 xTrans;

	//Get ship position
	Vector3 position = getPos(Globals::ship.toWorld);

	//Move ship left
	if(leftPressed)
	{
		//Set bounds
		if(position[0] > LBOUND)
		{
			xTrans.makeTranslate(-SHIP_SPEED, 0, 0);
			Globals::ship.toWorld = xTrans * Globals::ship.toWorld;
		}
	}

	//Move ship right
	if (rightPressed)
	{
		//Set bounds
		if(position[0] < RBOUND)
		{
			xTrans.makeTranslate(SHIP_SPEED, 0, 0);
			Globals::ship.toWorld = xTrans * Globals::ship.toWorld;
		}
	}
}

void Window::animateMissles()
{
	Sphere sphere = Sphere(3, 15, 15);

	Matrix4 yTrans;
	yTrans.makeTranslate(0, MISSLE_SPEED, 0);	//Applied each frame to move up

	//Draw missles
	for(int i = 0; i < MAX; i++)
	{
		if(mPositions[i])
		{
			Vector3 position = getPos(missles[i]);

			//Check bounds
			if(position[1] > UBOUND)
				mPositions[i] = 0; //Delete

			else //Render
			{
				sphere.toWorld = missles[i];
				sphere.draw();
			}
		}
	}

	//Translate up for next frame
	for(int i = 0; i < MAX; i++)
		if(mPositions[i])
			missles[i] = yTrans * missles[i];
}

void Window::spawnEnemy()
{
	Matrix4 xyTrans;

	int position = seekPosition(ePositions);

	if(position < MAX)
	{
		enemies[position].identity();

		//Each enemy has a random chance to spawn as a life giving enemy
		if (!(rand()%6))	//NOTE: Better determining function can probably be used. 6 works best for some reason
			givesLife[position] = 1;

		//Keep within window frame dimensions (positive / negative X-bounds)
		float xPos = rand() % (2*RBOUND) + LBOUND;
		xyTrans.makeTranslate(xPos, ESPAWN, 0);
		
		enemies[position] = xyTrans * enemies[position];
	}
}

void Window::animateEnemies()
{
	Sphere sphere = Sphere(8, 15, 15);

	Matrix4 yTrans;	//Applied each frame to move down
	yTrans.makeTranslate(0, -ENEMY_SPEED, 0);

	//Draw enemies
	for(int i = 0; i < MAX; i++)
	{
		if(ePositions[i])
		{
			Vector3 position = getPos(enemies[i]);
			
			//Check bounds
			if(position[1] <= FLOOR)
			{
				ePositions[i] = 0; //Delete
				givesLife[i] = 0;

				//Lose a life
				lifeTracker--;
			}

			else //Render
			{
				//Place at origin
				Globals::enemy.toWorld.identity();

				//Life giving enemies are indicated by a red color
				if (givesLife[i])
					Globals::enemy.material.color = red;
				else
					Globals::enemy.material.color = white;

				//Rotate to nose-down position
				Matrix4 rotate;
				rotate.makeRotateX(PI/2);
				Globals::enemy.toWorld = rotate * Globals::enemy.toWorld;
				rotate.makeRotateZ(PI);
				Globals::enemy.toWorld = rotate * Globals::enemy.toWorld;

				//Translate to position of enemy
				Matrix4 translate;
				translate.makeTranslate(position[0], position[1], position[2]);
				Globals::enemy.toWorld = translate * Globals::enemy.toWorld;
				Globals::enemy.draw(Globals::drawData);
			}
		}
	}

	//Translate down for next frame
	for(int i = 0; i < MAX; i++)
		if(ePositions[i])
			enemies[i] = yTrans * enemies[i];
}

//Finds next free position in an array
int Window::seekPosition(int list[])
{
	for(int i = 0; i < MAX; i++)
	{
		if(!list[i])
		{
			list[i] = 1;
			return i;
		}
	}

	return MAX;
}

//Extract X, Y, Z coords of an object
Vector3 Window::getPos(Matrix4 world)
{
	Vector4 pos(0, 0, 0, 1);

	pos = world * pos;
	return pos.toVector3();
}

//Called every new frame
void Window::updateTimers()
{
	frameCount++;

	//Put missles on cooldown after firing
	if(onCooldown)
		cooldownTimer++;

	if (cooldownTimer > COOLDOWN)
	{
		cooldownTimer = 0;
		onCooldown = false;
	}

	if (frameCount > SPAWN_DELAY)
	{
		frameCount = 0;
		spawnEnemy();
	}
}

void Window::drawHUD()
{
	//NOTE: Hardcoded positioning

	glPushMatrix();
	Matrix4 translate;
	Sphere life = Sphere(3, 15, 15);

	//Draw life icons
	for(int i = 0; i < lifeTracker; i++)
	{
		life.toWorld.identity();
		translate.makeTranslate(222, -55, 0);
		life.toWorld = translate * life.toWorld;

		for(int j = 0; j < i; j++)
		{
			translate.makeTranslate(-12, 0, 0);
			life.toWorld = translate * life.toWorld;
		}

		glColor3f(1, 0, 0);
		life.draw();
		glColor3f(1, 1, 1);
	}

	//Display score
	glTranslatef(170, -80, 0);
	glScalef(0.1, 0.1, 0.1);

	for(int i = 0; i < score.length(); i++)
		glutStrokeCharacter(GLUT_STROKE_ROMAN, score[i]);

	string scoreString = to_string(myScore);
	for(int i = 0; i < scoreString.length(); i++)
		glutStrokeCharacter(GLUT_STROKE_ROMAN, scoreString[i]);

	glPopMatrix();
}

void Window::hitDetector()
{
	//Checks distance from each missle to each ememy
	for(int i = 0; i < MAX; i++)
	{
		if(mPositions[i])
		{
			//get position of missle
			Vector3 posA = getPos(missles[i]);

			for(int j = 0; j < MAX; j++)
			{
				if(ePositions[j])
				{
					//Get position of enemy
					Vector3 posB = getPos(enemies[j]);

					//Check distance between missle and enemy
					if (checkDistance(posA, posB) < COLLISION)
					{
						//Hit detected
						//Set position for explosion instance
						int pos = seekPosition(exPositions);
						exPositions[pos] = 1;
						explosionEffect[pos].toWorld = enemies[j];
						explosionEffect[pos].EmitParticles();	//Begin emission

						mPositions[i] = 0;	//Destroy missle
						ePositions[j] = 0;	//Destroy enemy

						//Destroying a life giving enemy rewards player with 1 life
						//Number of lives is capped at 5
						if (givesLife[j] && lifeTracker < 5)
						{
							givesLife[j] = 0;	//Remove special indicator
							lifeTracker++;
						}

						myScore++;			//Increase score
					}
				}
			}
		}
	}

	//Update explosions
	for(int i = 0; i < MAX; i++)
	{
		if(exPositions[i])
			//Delete explosion if lifetime over
			if(!explosionEffect[i].Update())
				exPositions[i] = 0;
	}
}

float Window::checkDistance(Vector3 posA, Vector3 posB)
{
	float distance;
	Vector3 distanceVec = posB - posA;
	distance = distanceVec.magnitude();

	return distance;
}

void Window::gameOverScreen()
{
	//NOTE: Hardcoded positioning 

	glTranslatef(-40, 0, 0);
	glScalef(0.1, 0.1, 0.1);
	glColor3f(1, 1, 1);

	for(int i = 0; i < gameOver.length(); i++)
		glutStrokeCharacter(GLUT_STROKE_ROMAN, gameOver[i]);

	glTranslatef(-1170, -250, 0);

	for(int i = 0; i < instruct.length(); i++)
		glutStrokeCharacter(GLUT_STROKE_ROMAN, instruct[i]);
}