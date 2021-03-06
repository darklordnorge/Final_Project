/**
 * File:          map_building.c
 * Date:          15.03.2014
 * Description:   This file holds the main controll loop of the program.
 *                 it holds files which are taken or inspired by the Webots simulator
 * Author:        Stefan Klaus
 * Modifications: v1.0
 */

#include <webots/robot.h>
#include <webots/differential_wheels.h>
#include <webots/distance_sensor.h>
#include <math.h>
#include <webots/display.h> 
#include <stdio.h>

#include "functions.h"
#include "reference_points.h"
#include "map_building.h"
#include "e_puck_movement.h"
#include "odometry.h"
#include "e_puck_distance_sensors.h"


#define TIME_STEP 8
#define MAP_SIZE 70
#define CELL_SIZE 0.015

//occupancy code:
#define THRESHOLD_DIST 100
#define OCCUPANCE_DIST 150
#define LEFT 0        // Left side
#define RIGHT 1       // right side
// 8 IR proximity sensors
#define NUM_DIST_SENS 8

//states for the FSM
#define FORWARD 0
#define STOP 1
#define TURNRIGHT 2
#define TURNLEFT 3
#define UTURN 4

/* Definitions */
//Leds
WbDeviceTag led[3];

//display and map 
WbDeviceTag display;
WbImageRef background;
int display_width;
int display_height;
int map[MAP_SIZE][MAP_SIZE];

//robots initial positions on the map 
int robot_x = MAP_SIZE / 2;
int robot_y = MAP_SIZE / 2;  

//Distance sensors and corresponding arrays
int obstacle[NUM_DIST_SENS]={0,0,0,0,0,0,0,0};
bool ob_front, ob_right, ob_left;

// this is the angle at which the IR sensors are placed on the robot
float angle_offset[NUM_DIST_SENS] = {0.2793, 0.7854, 1.5708, 2.618, -2.618, -1.5708, -0.7854, -0.2793};

//movement
int new_encoder;

//starting state for the switch statement
int state = FORWARD;

/**
 * TAKEN FROM THE WEBOTS SIMULATOR
 * Initiate the display with a white color
 for all rooms but room 3 x & y = 0
 */
void init_display(){
	display = wb_robot_get_device("display");
	display_width = wb_display_get_width(display);
	display_height = wb_display_get_height(display);
	wb_display_fill_rectangle(display,0,0,display_width,display_height);
	background = wb_display_image_copy(display,0,0,display_width,display_height);
}

/**
 *	TAKEN FROM THE WEBOTS SIMULATOR
 * This function do the coordinate transform between the world coordinates (w)
 * and the map coordinates (m) in X and Y direction.
 */
static int wtom(float x)
{
	return (int)(MAP_SIZE / 2 + x / CELL_SIZE);
}

/**
 * TAKEN FROM THE WEBOTS SIMULATOR
 * Set the coresponding cell to 1 (occupied)
 * and display it
 */
void occupied_cell(int x, int y, float theta){

	// normalize angle
	while (theta > M_PI) {
		theta -= 2*M_PI;
	}
	while (theta < -M_PI) {
		theta += 2*M_PI;
	}

	// front cell
	if (-M_PI/6 <= theta && theta <= M_PI/6) {
		if (y+1 < MAP_SIZE) {
			map[x][y+1] = 1;
			wb_display_draw_rectangle(display,x,display_height-y,1,1);
		}
	}
	// right cell
	if (M_PI/3 <= theta && theta <= 2*M_PI/3) {
		if (x+1 < MAP_SIZE) {
			map[x+1][y] = 1;
			wb_display_draw_rectangle(display,x+1,display_height-1-y,1,1);
		}
	}
	// left cell
	if (-2*M_PI/3 <= theta && theta <= -M_PI/3) {
		if (x-1 > 0) {
			map[x-1][y] = 1;
			wb_display_draw_rectangle(display,x-1,display_height-1-y,1,1);
		}
	}
	// back cell
	if (5*M_PI/6 <= theta || theta <= -5*M_PI/6) {
		if (y-1 > 0) {
			map[x][y-1] = 1;
			wb_display_draw_rectangle(display,x,display_height-y-2,1,1);
		}
	}
}

/**
enables the needed sensor devices 
*/
void reset(){
	int i, j;
	
	//initialize the display
	init_display();
	//initialize the distance sensors
	init_distance_sensors(TIME_STEP);

	// map init to 0
	for (i = 0; i < MAP_SIZE; i++) {
		for (j = 0; j < MAP_SIZE; j++) {
			map[i][j] = 0;
		}
	}
	
	//emable and initialize the wheel encoders
	wb_differential_wheels_enable_encoders(TIME_STEP*4);
	wb_differential_wheels_set_encoders(0.0f, 0.0f);
	
	// initialize the LEDs ...
	led[0] = wb_robot_get_device("led0");
	led[1] = wb_robot_get_device("led2");
	led[2] = wb_robot_get_device("led6");
}

/**
Function to check for obstacles, sets the global boolean values
Used the make the run() code cleaner
*/
void checkObstacles(){
	int i; 
	int ps_offset[NUM_DIST_SENS] = {35,35,35,35,35,35,35,35};
	int *point_SensorData;

	point_SensorData = get_sensor_data(NUM_DIST_SENS);
	for(i = 0;i < NUM_DIST_SENS;i++){
		obstacle[i] = point_SensorData[i] - ps_offset[i] > THRESHOLD_DIST;
	}	
	//define boolean for sensor states for cleaner implementation
	ob_front  = 
	obstacle[0] ||
	obstacle[7];

	ob_right = 
	obstacle[2];

	ob_left = 
	obstacle[5];
}

/**
Main controll loop of the project.
Takes pointers to both teh global odometry and refernce point struct as parameters
*/
void run(struct odometryTrackStruct * ot, struct referencePos * ref){
	int i, it;
	int  direction;
	double cur_rot;
	int *point_SensorData;
	int ps_offset[NUM_DIST_SENS] = {35,35,35,35,35,35,35,35};
	double dSpeed = 500.0f;
	double dDistance = 0.01f; //0,01

	char thinking[] = "thinking";
	char text[] = "stopping";
	char no[] = "north";
	char ea[] = "east";
	char we[] = "west";
	char so[] = "south";
	
	robot_x = wtom(ot->result.x);
	robot_y = wtom(ot->result.y);
	
	point_SensorData = get_sensor_data(NUM_DIST_SENS);
	for(i = 0;i < NUM_DIST_SENS;i++){
		obstacle[i] = point_SensorData[i] - ps_offset[i] > THRESHOLD_DIST;
	}	
	//define boolean for sensor states for cleaner implementation
	ob_front  = 
	obstacle[0] ||
	obstacle[7];

	ob_right = 
	obstacle[2];

	ob_left = 
	obstacle[5];
	
	//mark cells as occupied
	wb_display_image_paste(display,background,0,0);
	wb_display_set_color(display,0x000000);
	for(i = 0;i < NUM_DIST_SENS;i++){
		if(point_SensorData[i] > OCCUPANCE_DIST){
			occupied_cell(robot_x, robot_y, ot->result.theta + angle_offset[i]);
		}
	}
	wb_display_image_delete(display,background);
	background = wb_display_image_copy(display,0,0,display_width,display_height);
	
	switch(state){
		case FORWARD:
			direction = check_direction(ot->result.theta); //check direction
			cur_rot = return_angle(ot->result.theta); //check rotation
			check_reference_points(ot, ref); //checks for reference point
		 	if(direction == 1){ //if direction 1(north)
				printf("%s\n", no); 
				check_rotation(cur_rot, 90, dSpeed); //check if rotation is 90 degrees
				move_forward(dSpeed, dDistance, ot);
			}else if(direction == 2){
				printf("%s\n", ea);
				check_rotation(cur_rot, 0, dSpeed);
				move_forward(dSpeed, dDistance, ot);
			}else if(direction == 3){
				printf("%s\n", so);
				check_rotation(cur_rot, 270, dSpeed);
				move_forward(dSpeed, dDistance, ot);
			}else if(direction == 4){
				printf("%s\n", we);
				check_rotation(cur_rot, 180, dSpeed);
				move_forward(dSpeed, dDistance, ot);
			} 
			 if(ob_front){
				state = STOP;
				}
			break;			
		case STOP:
			stop_robot(); //stops he robot
			printf("%s\n", text); 
			odometry_track_step(ot); //updates the odometry struct
			direction = check_direction(ot->result.theta); //check which direction the robot moves in 
		 	if(ob_front && ob_left && direction == 1){
				state = TURNRIGHT;
				}
			 else if(ob_front && ob_left){
				state = UTURN;
			} 
			else if(ob_front && ob_right && direction == 2){
				state = TURNLEFT;
			}
			else if(ob_front && ob_right){
				state = UTURN;
			}
			else if(ob_front){
				state = UTURN;
				}   
			break;	
			
		case TURNRIGHT:
			turn_right(dSpeed);
			state = FORWARD;
			break;
		case TURNLEFT:
			turn_left(dSpeed);
			state = FORWARD;
			break;
		case UTURN:
		/*	
			this looks very heavy and clumpsy
			but it only repeats the same steps for each direction
			this was the only apprent solution
		*/
			direction = check_direction(ot->result.theta); //check which direction to robot moves in
			printf("%d\n",direction ); //print direction
			if(direction == 1){
				printf("%s\n", no); 
				turn_left(dSpeed); 
				for(it = 0;it < 5;it++){ //for loop which check if refrence points are reached, obstacles encountred and cells to map
					printf("%s\n",thinking);
					check_reference_points(ot, ref);

					if((ob_front && ob_right) || (ob_front && ob_left)){ //check if obstacles are in front, if so check for reference points and stop the robot
						check_reference_points(ot, ref);
						state = STOP;
						break;
					}
					move_forward(dSpeed, dDistance, ot);
					//mark cells as occupied
					wb_display_image_paste(display,background,0,0);
					wb_display_set_color(display,0x000000);
					point_SensorData = get_sensor_data(NUM_DIST_SENS);
					for(i = 0;i < NUM_DIST_SENS;i++){
						if(point_SensorData[i] > OCCUPANCE_DIST){
							occupied_cell(robot_x, robot_y, ot->result.theta + angle_offset[i]);
						}
					}
					wb_display_image_delete(display,background);
					background = wb_display_image_copy(display,0,0,display_width,display_height); 
				}

				turn_left(dSpeed);
				odometry_track_step(ot); //update odometry data
				state = FORWARD;
			}else if(direction == 2){
				printf("%s\n", ea);
				turn_right(dSpeed);
				for(it = 0;it < 5;it++){
					printf("%s\n",thinking);
					check_reference_points(ot, ref);
					if((ob_front && ob_right) || (ob_front && ob_left)){
						check_reference_points(ot, ref);
						state = STOP;
						break;
					}else{
						move_forward(dSpeed, dDistance, ot);
						//mark cells as occupied
						wb_display_image_paste(display,background,0,0);
						wb_display_set_color(display,0x000000);
						point_SensorData = get_sensor_data(NUM_DIST_SENS);
						for(i = 0;i < NUM_DIST_SENS;i++){
							if(point_SensorData[i] > OCCUPANCE_DIST){
								occupied_cell(robot_x, robot_y, ot->result.theta + angle_offset[i]);
							}
						}
					 	wb_display_image_delete(display,background);
						background = wb_display_image_copy(display,0,0,display_width,display_height); 
					}
				}
				turn_right(dSpeed);
				odometry_track_step(ot);
				state = FORWARD;
			}else if(direction == 3){
				printf("%s\n", so);
				turn_right(dSpeed);
				for(it = 0;it < 5;it++){
					printf("%s\n",thinking);
					check_reference_points(ot, ref);					
					if((ob_front && ob_right) || (ob_front && ob_left)){
						printf("%s\n", text);
						check_reference_points(ot, ref);
						state = STOP;
						break;
					}else{
						//mark cells as occupied
						wb_display_image_paste(display,background,0,0);
						wb_display_set_color(display,0x000000);
						point_SensorData = get_sensor_data(NUM_DIST_SENS);
						for(i = 0;i < NUM_DIST_SENS;i++){
							if(point_SensorData[i] > OCCUPANCE_DIST){
								occupied_cell(robot_x, robot_y, ot->result.theta + angle_offset[i]);
							}
							
						}
						 wb_display_image_delete(display,background);
						background = wb_display_image_copy(display,0,0,display_width,display_height); 
						
						move_forward(dSpeed, dDistance, ot);
					}
				}

				turn_right(dSpeed);
				odometry_track_step(ot);
				state = FORWARD;
			}else if(direction == 4){
				printf("%s\n", we);
				turn_left(dSpeed);
				for(it = 0;it < 5;it++){
					printf("%s\n",thinking);
					check_reference_points(ot, ref);					
					if((ob_front && ob_right) || (ob_front && ob_left)){

						printf("%s\n", text);
						state = STOP;
						break;
					}
					move_forward(dSpeed, dDistance, ot);
					//mark cells as occupied
					wb_display_image_paste(display,background,0,0);
					wb_display_set_color(display,0x000000);
					point_SensorData = get_sensor_data(NUM_DIST_SENS);
					for(i = 0;i < NUM_DIST_SENS;i++){
						if(point_SensorData[i] > OCCUPANCE_DIST){
							occupied_cell(robot_x, robot_y, ot->result.theta + angle_offset[i]);
						}
					}
					wb_display_image_delete(display,background);
					background = wb_display_image_copy(display,0,0,display_width,display_height);
				}
			
				turn_left(dSpeed);
				odometry_track_step(ot);
				state = FORWARD;
			}
			break;
		 default:
			state = FORWARD; 
	}
	wb_display_set_color(display,0xFF0000);
    wb_display_draw_rectangle(display,robot_x, display_height-robot_y-1,1,1);
}