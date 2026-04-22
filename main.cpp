#include<iostream>
#include<vector>
#include<map>
#include<algorithm>

#include"imgui.h"
#include"imgui_impl_glfw.h"
#include"imgui_impl_opengl3.h"
#include<glad/glad.h>
#include<GLFW/glfw3.h>
#include<glm/glm.hpp>
#include<cmath>

#include"shaderClass.h"
#include"VAO.h"
#include"VBO.h"
#include"EBO.h"

const double _pi = 3.14159265358979323846;
const double _c = 299792458; // in m/s
const double _G = 6.674e-11;

double SPACE_SCALE = 1e-8;
glm::dvec2 SPACE_OFFSET(0.0, 0.0);
double TIME_SCALE = 60;
double WINDOW_SIZE = 800;
bool RIGHT_DOWN = false;
glm::vec2 old_MOUSE_POS;

GLuint raySSBO = 0;

struct Object {
	double mass;		// in kg
	double diameter;	// in km
	double r_s;			// in km
	bool dead;

	glm::dvec2 pos;		// in km
	glm::dvec2 vel;		// in m/s

	glm::vec3 color;	// RGB value

	Object(double mass, double diameter, glm::dvec2 pos, glm::dvec2 vel, glm::vec3 color) : mass(mass), diameter(diameter), r_s((2 * _G * mass) / (_c * _c) / 1000.0), pos(pos), vel(vel), color(color), dead(false) {}

	void update() {
		this->r_s = (2 * _G * mass) / (_c * _c) / 1000.0;
	}

	bool operator<(const Object& other) const {
		return this->mass < other.mass;
	}
};

std::vector<Object> Objects = {
	Object(8.15e36, 1, glm::dvec2(0.0, 0.0), glm::dvec2(0.0, 0.0), glm::vec3(1.0f, 0.0f, 0.0f))
};
Object& BLACK_HOLE = Objects.at(0);

struct Ray {
	glm::dvec2 pos; // polar coordinates to BLACK_HOLE; r (r) in km, phi (g) in radians
	glm::dvec2 vel; // velocity of the polar coordinates to BLACK_HOLE; r (r) in km/s, phi (g) in radians

	glm::dvec2 cartPos; // in km

	bool dead = false;

	std::vector<glm::dvec2> trail;
	glm::vec3 color = glm::vec3(1.0f, 1.0f, 1.0f);

	Ray(glm::dvec2 pos, glm::dvec2 vel) : cartPos(pos) {
		vel = glm::normalize(vel) * (_c / 1000.0);

		glm::dvec2 p = pos - BLACK_HOLE.pos;
		double r = glm::length(p);

		this->pos = glm::dvec2(r, atan2(p.y, p.x));
		this->vel = glm::dvec2(
			glm::dot(p, vel) / r,
			(p.x * vel.y - p.y * vel.x) / (r * r)
		);
	}

	glm::dvec2 accel(glm::dvec2 pos, glm::dvec2 vel) {
		if (pos.r < BLACK_HOLE.r_s) {
			this->dead = true;
			return glm::dvec2(0.0);
		}

		return glm::dvec2(
			vel.g * vel.g * (pos.r - 1.5 * BLACK_HOLE.r_s),
			-2.0 * vel.r * vel.g / pos.r
		);
	}

	void move(double deltaTime) {
		if (!this->dead) this->trail.push_back(this->cartPos);
		else this->trail.push_back(BLACK_HOLE.pos);
		if (this->trail.size() > 1000) this->trail.erase(trail.begin());

		if (this->dead) return;

		int substeps = std::clamp((int)(1000.0 * BLACK_HOLE.r_s / this->pos.r), 1, 1000) + 15;
		double subDelta = (deltaTime * TIME_SCALE) / substeps;
		for (int i = 0; i < substeps; i++) {
			glm::dvec2 p = this->pos;
			glm::dvec2 v = this->vel;

			glm::dvec2 k1p = v;
			glm::dvec2 k1v = accel(p, v);

			glm::dvec2 k2p = v + k1v * (subDelta / 2.0);
			glm::dvec2 k2v = accel(p + k1p * (subDelta / 2.0), v + k1v * (subDelta / 2.0));

			glm::dvec2 k3p = v + k2v * (subDelta / 2.0);
			glm::dvec2 k3v = accel(p + k2p * (subDelta / 2.0), v + k2v * (subDelta / 2.0));

			glm::dvec2 k4p = v + k3v * subDelta;
			glm::dvec2 k4v = accel(p + k3p * subDelta, v + k3v * subDelta);

			this->pos += (subDelta / 6.0) * (k1p + 2.0 * k2p + 2.0 * k3p + k4p);
			this->vel += (subDelta / 6.0) * (k1v + 2.0 * k2v + 2.0 * k3v + k4v);
		}

		this->cartPos = glm::dvec2(this->pos.r * cos(this->pos.g), this->pos.r * sin(this->pos.g)) + BLACK_HOLE.pos;

		for (Object& object : Objects) {
			if (glm::length(this->cartPos - object.pos) < object.diameter / 2.0) {
				this->color = object.color;
				this->dead = true;
			}
		}
	}
};

std::vector<Ray> Rays = {};

struct GPURay {
	glm::dvec2 pos;
	glm::dvec2 vel;
	glm::vec2 cartPos;
	int dead;
	int pad1; int pad2; int pad3; int pad4; int pad5;
};

std::vector<GPURay> gpuRays = {};

GPURay makeRay(glm::dvec2 pos, glm::dvec2 dir) {
	GPURay ray;

	dir = glm::normalize(dir) * (_c / 1000.0);

	glm::dvec2 p = pos - BLACK_HOLE.pos;
	double r = glm::length(p);

	ray.cartPos = glm::vec2(pos);
	ray.pos = glm::dvec2(r, atan2(p.y, p.x));
	ray.vel = glm::dvec2(
		glm::dot(p, dir) / r,
		(p.x * dir.y - p.y * dir.x) / (r * r)
	);
	ray.dead = 0;
	ray.pad1 = ray.pad2 = ray.pad3 = ray.pad4 = ray.pad5 = 0;

	return ray;
}

void init_rays(int amount, glm::dvec2 pos) {
	for (int i = 0; i < amount; i++) {
		float angle = ((360.0f / (amount)) * i) * (_pi / 180.0f);
		Rays.push_back(Ray(pos, glm::dvec2(cos(angle), sin(angle))));
	}

	/*glBindBuffer(GL_SHADER_STORAGE_BUFFER, raySSBO);
	glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, gpuRays.size() * sizeof(GPURay), gpuRays.data());

	GPURay* check = (GPURay*)glMapBuffer(GL_SHADER_STORAGE_BUFFER, GL_READ_ONLY);
	std::cout << "pre-dispatch ray[0] dead: " << check[0].dead
		<< " Xpos: " << check[0].pos.x << std::endl;
	glUnmapBuffer(GL_SHADER_STORAGE_BUFFER);*/

	/*for (int i = 0; i < amount; i++) {
		Rays.push_back(Ray(glm::dvec2(-1e8, (20e7 / amount * i) - 10e7), glm::dvec2(1.0, 0.0)));
	}*/
}

std::vector<GLfloat> generateCircle(float cx, float cy, float r, int sectors,
	float r_col, float g_col, float b_col)
{
	std::vector<GLfloat> verts;

	// Center vertex
	verts.insert(verts.end(), { cx, cy, 0.0f,  r_col, g_col, b_col });

	// Outer vertices
	for (int i = 0; i <= sectors; i++)
	{
		float angle = 2.0f * _pi * i / sectors;
		float x = cx + r * cos(angle);
		float y = cy + r * sin(angle);
		verts.insert(verts.end(), { x, y, 0.0f,  r_col, g_col, b_col });
	}

	return verts;
}

std::vector<GLuint> generateCircleIndices(int sectors) {
	std::vector<GLuint> indices;
	for (int i = 1; i <= sectors; i++) {
		indices.insert(indices.end(), { 0, (GLuint)i, (GLuint)(i + 1) });
	}
	return indices;
}

void mouse_callback(GLFWwindow* window, int button, int action, int mods) {
	if (ImGui::GetIO().WantCaptureMouse) return;
	if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
		double xpos, ypos;
		glfwGetCursorPos(window, &xpos, &ypos);

		// screen coords -> world coords, y flipped in OpenGL
		double worldX = (xpos / WINDOW_SIZE * 2.0 - 1.0 - SPACE_OFFSET.x) / SPACE_SCALE;
		double worldY = ((WINDOW_SIZE - ypos) / WINDOW_SIZE * 2.0 - 1.0 - SPACE_OFFSET.y) / SPACE_SCALE;

		init_rays(180, glm::dvec2(worldX, worldY));
		/*double r = sqrt(worldX * worldX + worldY * worldY);
		std::cout << "clicked at: " << worldX << ", " << worldY
			<< " | r = " << r << " km"
			<< " | r_s = " << BLACK_HOLE.r_s << " km" << std::endl;*/
	}
	if (button == GLFW_MOUSE_BUTTON_RIGHT && action == GLFW_PRESS) {
		RIGHT_DOWN = true;
		double oldMouseX; double oldMouseY;
		glfwGetCursorPos(window, &oldMouseX, &oldMouseY);
		old_MOUSE_POS = glm::vec2(oldMouseX,oldMouseY);
	}
	if (button == GLFW_MOUSE_BUTTON_RIGHT && action == GLFW_RELEASE) {
		RIGHT_DOWN = false;
	}
}

void scroll_callback(GLFWwindow* window, double xoffset, double yoffset) {
	double mouseX, mouseY;
	glfwGetCursorPos(window, &mouseX, &mouseY);

	double ndcX = mouseX / WINDOW_SIZE * 2.0 - 1.0;
	double ndcY = (WINDOW_SIZE - mouseY) / WINDOW_SIZE * 2.0 - 1.0;

	double factor = (yoffset > 0) ? 1.1 : 0.9;

	SPACE_OFFSET.x = ndcX + (SPACE_OFFSET.x - ndcX) * factor;
	SPACE_OFFSET.y = ndcY + (SPACE_OFFSET.y - ndcY) * factor;

	SPACE_SCALE *= factor;
}

void cursor_callback(GLFWwindow* window, double xpos, double ypos) {
	if (RIGHT_DOWN) {
		SPACE_OFFSET.x += (xpos - old_MOUSE_POS.x) / (WINDOW_SIZE / 2);
		SPACE_OFFSET.y -= (ypos - old_MOUSE_POS.y) / (WINDOW_SIZE / 2);
		old_MOUSE_POS.x = xpos;
		old_MOUSE_POS.y = ypos;
	}
}

int main() {
	glfwInit();

	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

	GLFWwindow* window = glfwCreateWindow(WINDOW_SIZE, WINDOW_SIZE, "Black Hole Simulation - Starting...", NULL, NULL);
	if (window == NULL) {
		std::cout << "Failed to create GLFW window" << std::endl;
		glfwTerminate();
		return -1;
	}
	glfwMakeContextCurrent(window);
	glfwSetMouseButtonCallback(window, mouse_callback);
	glfwSetScrollCallback(window, scroll_callback);
	glfwSetCursorPosCallback(window, cursor_callback);

	gladLoadGL();

	glViewport(0, 0, WINDOW_SIZE, WINDOW_SIZE);

	int sectors = 64;
	auto circleVerts = generateCircle(0.0f, 0.0f, 1.0f, sectors, 1.0f, 1.0f, 1.0f);
	auto circleIndices = generateCircleIndices(sectors);

	Shader shaderProgram("shaders/default.vert", "shaders/default.frag");
	Shader computeShader("shaders/move_rays.comp");

	VAO VAO1;
	VAO1.Bind();

	VBO VBO1(circleVerts.data(), circleVerts.size() * sizeof(GLfloat));
	EBO EBO1(circleIndices.data(), circleIndices.size() * sizeof(GLuint));

	VAO1.Bind();

	VBO1.Bind();
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(GLfloat), (void*)0);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(GLfloat), (void*)(3 * sizeof(GLfloat)));
	glEnableVertexAttribArray(1);

	EBO1.Bind();

	VAO1.Unbind();
	VBO1.Unbind();
	EBO1.Unbind();

	glGenBuffers(1, &raySSBO);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, raySSBO);
	glBufferData(GL_SHADER_STORAGE_BUFFER, 100000/*maxrays*/ * sizeof(GPURay), nullptr, GL_DYNAMIC_DRAW);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, raySSBO);

	GLint uniScale = glGetUniformLocation(shaderProgram.ID, "scale");
	GLint uniOffset = glGetUniformLocation(shaderProgram.ID, "offset");
	GLint uniColor = glGetUniformLocation(shaderProgram.ID, "customColor");

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO(); (void)io;
	ImGui::StyleColorsDark();
	ImGui_ImplGlfw_InitForOpenGL(window, true);
	ImGui_ImplOpenGL3_Init("#version 330");

	double lastTime = glfwGetTime();
	double timePassed = 0;
	int fps = -1;

	double lastTitleUpdate = 0.0;
	int displayFps = fps;
	float displayFrameTime = 0.0f;

	while (!glfwWindowShouldClose(window)) {
		double currentTime = glfwGetTime();
		float deltaTime = currentTime - lastTime;

		if (deltaTime > 0) fps = 1 / (currentTime - lastTime); else fps = -1;
		deltaTime = std::min(deltaTime, 0.1f);

		timePassed += (double)deltaTime * TIME_SCALE;

		if (currentTime - lastTitleUpdate >= 0.6) {
			displayFps = fps;
			displayFrameTime = deltaTime;
			lastTitleUpdate = currentTime;
		}

		std::string title = "Black Hole Simulation - " + std::to_string(displayFps) + " fps";
		glfwSetWindowTitle(window, title.c_str());

		glClearColor(0.02f, 0.02f, 0.02f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT);

		/*computeShader.Activate();
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, raySSBO);
		glUniform1f(glGetUniformLocation(computeShader.ID, "r_s"), (float)BLACK_HOLE.r_s);
		glUniform1f(glGetUniformLocation(computeShader.ID, "deltaTime"), (float)(deltaTime * TIME_SCALE));
		glUniform1i(glGetUniformLocation(computeShader.ID, "rayCount"), (int)gpuRays.size());
		glDispatchCompute((gpuRays.size() + 63) / 64, 1, 1);
		glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);*/

		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();

		shaderProgram.Activate();
		VAO1.Bind();

		for (Object& object : Objects) {
			object.update();
		}


		glBindBuffer(GL_SHADER_STORAGE_BUFFER, raySSBO);
		GPURay* data = (GPURay*)glMapBuffer(GL_SHADER_STORAGE_BUFFER, GL_READ_ONLY);

		/*if (gpuRays.size() > 0) {
			std::cout << "ray[0] pos: " << data[0].pos.x << ", " << data[0].pos.y << std::endl;
			std::cout << "ray[0] dead: " << data[0].dead << std::endl;
		}*/

		int activeRays = 0;
		for (int i = 0; i < gpuRays.size(); i++) {
			if (!data[i].dead) activeRays++;
		}

		/*for (int i = 0; i < (int)gpuRays.size(); i++) {
			if (data[i].dead) continue;

			double r = data[i].pos.x;
			double phi = data[i].pos.y;
			glm::dvec2 dot = glm::dvec2(r * cos(phi), r * sin(phi)) + BLACK_HOLE.pos;

			glUniform1f(uniScale, 0.002);
			glUniform2f(uniOffset, SPACE_SCALE * dot.x + SPACE_OFFSET.x, SPACE_SCALE * dot.y + SPACE_OFFSET.y);
			glUniform3f(uniColor, 1.0f, 1.0f, 1.0f);
			glDrawElements(GL_TRIANGLES, circleIndices.size(), GL_UNSIGNED_INT, 0);
		}

		glUnmapBuffer(GL_SHADER_STORAGE_BUFFER);*/

		for (Ray& ray : Rays) {
			ray.move(deltaTime);

			for (int i = 0; i < ray.trail.size(); i++) {
				glm::dvec2 dot = ray.trail[i];

				float alpha = (float)i / ray.trail.size();

				glUniform1f(uniScale, 0.002);
				glUniform2f(uniOffset, SPACE_SCALE * dot.x + SPACE_OFFSET.x, SPACE_SCALE * dot.y + SPACE_OFFSET.y);
				glUniform3f(uniColor, alpha * ray.color.r, alpha * ray.color.g, alpha * ray.color.b);

				glDrawElements(GL_TRIANGLES, circleIndices.size(), GL_UNSIGNED_INT, 0);
			}
		}


		for (Object& object : Objects) {
			double renderRadius = std::max(object.r_s, object.diameter / 2.0);
			glUniform1f(uniScale, std::max(SPACE_SCALE * renderRadius, 0.002));
			glUniform2f(uniOffset, SPACE_SCALE * object.pos.x + SPACE_OFFSET.x, SPACE_SCALE * object.pos.y + SPACE_OFFSET.y);
			glUniform3f(uniColor, object.color.r, object.color.g, object.color.b);

			glDrawElements(GL_TRIANGLES, circleIndices.size(), GL_UNSIGNED_INT, 0);
		}

		lastTime = currentTime;

		ImGui::Begin("Black Hole Simulation");
		double mass_solar = BLACK_HOLE.mass / 1.989e30;
		double min_sol = 1e35 / 1.989e30, max_sol = 1e38 / 1.989e30;
		double min_time = 1.0, max_time = 1000.0;

		if (ImGui::SliderScalar("Mass", ImGuiDataType_Double, &mass_solar, &min_sol, &max_sol, "%.3gMsol", ImGuiSliderFlags_Logarithmic)) BLACK_HOLE.mass = mass_solar * 1.989e30;
		ImGui::Text("Schwarzschild radius: %.2g km\n\n", BLACK_HOLE.r_s);

		if (ImGui::SliderScalar("Time Scale", ImGuiDataType_Double, &TIME_SCALE, &min_time, &max_time, "%.0f")) TIME_SCALE = round(TIME_SCALE);
		ImGui::Text("Time Passed: %.0f min %.0f s\n\n", floor(timePassed / 60), fmod(timePassed, 60.0));

		ImGui::Text("FPS: %d", displayFps);
		ImGui::Text("Frame Time: %.3f ms", displayFrameTime * 1000.0f);
		ImGui::Text("Active Rays: %d\n\n", activeRays);

		if (ImGui::Button("Clear Rays")) {
			Rays.clear();
			/*gpuRays.clear();
			glBindBuffer(GL_SHADER_STORAGE_BUFFER, raySSBO);
			glBufferData(GL_SHADER_STORAGE_BUFFER, 100000 * sizeof(GPURay), nullptr, GL_DYNAMIC_DRAW);*/
		}
		ImGui::End();

		ImGui::Render();
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

		glfwSwapBuffers(window);
		glfwPollEvents();
	}

	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	ImGui::DestroyContext();

	VAO1.Delete();
	VBO1.Delete();
	EBO1.Delete();
	shaderProgram.Delete();

	glfwDestroyWindow(window);
	glfwTerminate();
	return 0;
}