#include "core/GameCode.hpp"


// Base on disassembly of the math.dll. Could be simplified a lot.
void GameCode::rotateCameraFrame(glm::mat4& mat, const glm::vec3& axis, float angle, uint flag){
	float *pfVar2 = nullptr;
	float *pfVar3 = nullptr;
	int iVar4 = 0;
	float local_100 = 1.0f;
	float local_fc = 0.0f;
	float local_f8 = 0.0f;
	float local_f4 = 0.0f;

	float local_f0 = 0.0f;
	float local_ec = 1.0f;
	float local_e8 = 0.0f;
	float local_e4 = 0.0f;

	float local_e0 = 0.0f;
	float local_dc = 0.0f;
	float local_d8 = 1.0f;
	float local_d4 = 0.0f;

	float local_d0 = 0.0f;
	float local_cc = 0.0f;
	float local_c8 = 0.0f;
	float local_c4 = 1.f;

	float local_c0 = 1.0f;
	float local_bc = 0.0f;
	float local_b8 = 0.0f;
	float local_b4 = 0.0f;

	float local_b0 = 0.0f;
	float local_ac = 1.0f;
	float local_a8 = 0.0f;
	float local_a4 = 0.0f;

	float local_a0 = 0.0f;
	float local_9c = 0.0f;
	float local_98 = 1.0f;
	float local_94 = 0.0f;

	float local_90 = 0.0f;
	float local_8c = 0.0f;
	float local_88 = 0.0f;
	float local_84 = 1.0f;

	glm::mat4 local_80_back = glm::mat4(1.0f);
	float* local_80 = &local_80_back[0][0];
	float local_64 = 0.0f;
	float local_60 = 0.0f;
	float local_5c;
	float local_58;
	float local_54 = 0.0f;
	float local_50 = 0.0f;
	float local_4c = 0.0f;
	float local_48 = 0.0f;
	float local_44 = 1.0f;
	glm::mat4 local_40 = glm::mat4(1.0f);

	if (axis[0] != 0.0) {
		float fVar5 = (float)(angle * 0.01745329f * axis[0]);
		float fVar1 = (float)std::cos(fVar5);
		fVar5 = (float)std::sin(fVar5);
		local_80[5] = fVar1;
		local_80[6] = fVar5;
		local_80[4] = 0.0;
		local_5c = -local_80[6];
		local_58 = local_80[5];
		local_64 = 0.0;
		local_60 = 0.0;
		local_54 = 0.0;
	}
	if (axis[1] != 0.0) {
		float local_104 = (angle * 0.01745329f * axis[1]);
		float fVar1 = std::cos(local_104);
		float fVar5 = std::sin(local_104);
		local_100 = fVar1;
		local_e0 = fVar5;
		local_fc = 0.0;
		local_f4 = 0.0;
		local_dc = 0.0;
		local_d4 = 0.0;
		local_f8 = -local_e0;
		local_d8 = local_100;
	}
	if (axis[2] != 0.0) {
		float local_104 = (angle * 0.01745329f * axis[2]);
		float fVar1 = std::cos(local_104);
		float fVar5 = std::sin(local_104);
		local_c0 = fVar1;
		local_bc = fVar5;
		local_b0 = -local_bc;
		local_b8 = 0.0;
		local_b4 = 0.0;
		local_a8 = 0.0;
		local_a4 = 0.0;
		local_ac = local_c0;
	}

	if (flag == 0) {
		if (axis[0] != 0.0) {
			mat = local_80_back;
		}
		if (axis[1] != 0.0) {
			local_40 = glm::mat4(1.0f);
			iVar4 = 4;
			pfVar2 = &mat[0][0];
			pfVar3 = &local_40[0][0];
			do {
				iVar4 = iVar4 + -1;
				*pfVar3 = local_d0 * pfVar2[3] +
				local_f0 * pfVar2[1] + local_e0 * pfVar2[2] + local_100 * pfVar2[0];
				pfVar3[1] = local_dc * pfVar2[2] +
				local_fc * pfVar2[0] + local_cc * pfVar2[3] + local_ec * pfVar2[1];
				pfVar3[2] = local_d8 * pfVar2[2] +
				(float)local_f8 * pfVar2[0] + local_c8 * pfVar2[3] + local_e8 * pfVar2[1];
				pfVar3[3] = local_d4 * pfVar2[2] +
				local_f4 * pfVar2[0] + local_c4 * pfVar2[3] + local_e4 * pfVar2[1];
				pfVar2 = pfVar2 + 4;
				pfVar3 = pfVar3 + 4;
			} while (iVar4 != 0);
			local_80_back = local_40;
			mat = local_80_back;
		}
		if (axis[2] != 0.0) {
			local_40 = glm::mat4(1.0f);
			iVar4 = 4;
			pfVar2 = &mat[0][0];
			pfVar3 = &local_40[0][0];
			do {
				iVar4 = iVar4 + -1;
				*pfVar3 = local_a0 * pfVar2[2] +
				local_c0 * pfVar2[0] + local_b0 * pfVar2[1] + local_90 * pfVar2[3];
				pfVar3[1] = local_9c * pfVar2[2] +
				local_bc * pfVar2[0] + local_ac * pfVar2[1] + local_8c * pfVar2[3];
				pfVar3[2] = local_98 * pfVar2[2] +
				local_b8 * pfVar2[0] + local_a8 * pfVar2[1] + local_88 * pfVar2[3];
				pfVar3[3] = local_94 * pfVar2[2] +
				local_b4 * pfVar2[0] + local_a4 * pfVar2[1] + local_84 * pfVar2[3];
				pfVar2 = pfVar2 + 4;
				pfVar3 = pfVar3 + 4;
			} while (iVar4 != 0);
			local_80_back = local_40;
			mat = local_80_back;
			return;
		}
	}
	else if (flag == 1) {
		if (axis[0] != 0.0) {
			local_40 = glm::mat4(1.0f);
			iVar4 = 4;
			pfVar2 = &mat[0][0];
			pfVar3 = &local_40[0][0];
			do {
				iVar4 = iVar4 + -1;
				*pfVar3 = local_50 * pfVar2[3] +
				local_80[0] * pfVar2[0] + local_60 * pfVar2[2] + local_80[4] * pfVar2[1];
				pfVar3[1] = local_80[5] * pfVar2[1] +
				local_4c * pfVar2[3] + local_80[1] * pfVar2[0] + local_5c * pfVar2[2];
				pfVar3[2] = local_80[6] * pfVar2[1] +
				local_48 * pfVar2[3] + local_80[2] * pfVar2[0] + local_58 * pfVar2[2];
				pfVar3[3] = local_64 * pfVar2[1] +
				local_44 * pfVar2[3] + local_80[3] * pfVar2[0] + local_54 * pfVar2[2];
				pfVar2 = pfVar2 + 4;
				pfVar3 = pfVar3 + 4;
			} while (iVar4 != 0);
			local_80_back = local_40;
			mat = local_80_back;
		}
		if (axis[1] != 0.0) {
			local_40 = glm::mat4(1.0f);
			iVar4 = 4;
			pfVar2 = &mat[0][0];
			pfVar3 = &local_40[0][0];
			do {
				iVar4 = iVar4 + -1;
				*pfVar3 = local_e0 * pfVar2[2] +
				local_100 * pfVar2[0] + local_d0 * pfVar2[3] + local_f0 * pfVar2[1];
				pfVar3[1] = local_dc * pfVar2[2] +
				local_fc * pfVar2[0] + local_cc * pfVar2[3] + local_ec * pfVar2[1];
				pfVar3[2] = local_d8 * pfVar2[2] +
				local_f8 * pfVar2[0] + local_c8 * pfVar2[3] + local_e8 * pfVar2[1];
				pfVar3[3] = local_d4 * pfVar2[2] +
				local_f4 * pfVar2[0] + local_c4 * pfVar2[3] + local_e4 * pfVar2[1];
				pfVar2 = pfVar2 + 4;
				pfVar3 = pfVar3 + 4;
			} while (iVar4 != 0);
			local_80_back = local_40;
			mat = local_80_back;
		}
		if (axis[2] != 0.0) {
			local_40 = glm::mat4(1.0f);
			iVar4 = 4;
			pfVar2 = &mat[0][0];//(float *)(mat + 8);
			pfVar3 = &local_40[0][0];
			do {
				iVar4 = iVar4 + -1;
				*pfVar3 = local_a0 * pfVar2[2] +
				local_90 * pfVar2[3] + local_c0 * pfVar2[0] + local_b0 * pfVar2[1];
				pfVar3[1] = local_bc * pfVar2[0] +
				local_ac * pfVar2[1] + local_9c * pfVar2[2] + local_8c * pfVar2[3];
				pfVar3[2] = local_b8 * pfVar2[0] +
				local_a8 * pfVar2[1] + local_98 * pfVar2[2] + local_88 * pfVar2[3];
				pfVar3[3] = local_b4 * pfVar2[0] +
				local_a4 * pfVar2[1] + local_94 * pfVar2[2] + local_84 * pfVar2[3];
				pfVar2 = pfVar2 + 4;
				pfVar3 = pfVar3 + 4;
			} while (iVar4 != 0);
			local_80_back = local_40;
			mat = local_80_back;
			return;
		}
	} else if(flag == 2){
		Log::error("Unsupported.");
	}
	return;
}

// Based on disassembly of the game.dll.
glm::mat4 GameCode::cameraRotationMatrix(float rotationX, float rotationY){

	float deltaRotX = 0.0f;
	float deltaRotY = 0.0f;
	float initRotationX = rotationX;
	float initRotationY = rotationY;
	float rotationLimitX = 0.0f;
	float rotationLimitY = 0.0f;

	float angleX = glm::clamp(deltaRotX + rotationX, initRotationX - rotationLimitX, initRotationX + rotationLimitX);
	float angleY = glm::clamp(deltaRotY + rotationY, initRotationY - rotationLimitY, initRotationY + rotationLimitY);

	glm::mat4 rotMat = glm::mat4(1.0f);
	rotateCameraFrame(rotMat, glm::vec3(0.0f, 1.0f, 0.0f), angleY,0);
	rotateCameraFrame(rotMat, glm::vec3(rotMat[0]), angleX, 1);
	return rotMat;
}
