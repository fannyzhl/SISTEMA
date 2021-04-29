#include <Servo.h>

//#define DEBUG_SERVO
//#define DEBUG_CONV
//#define DEBUG_POS
//#define DEBUG_MOVE
//#define VERIFY_CONV

typedef struct {
  Servo servo;
  int pin;
  int min_pulse_width;
  int max_pulse_width;
  double max_angle;
  double multiplier;
} ServoInfo;

ServoInfo g_shoulderServoInfo, g_elbowServoInfo;

const double g_innerArmLength = 80.0, g_outerArmLength = 80.0;  /* Expresado en mm. */

double g_posX = 0.0, g_posY = 0.0;
bool g_absPos = false;

void setup() {
  /* Validar ángulos. No hacer nada si representan valores inválidos. */
  if (g_innerArmLength <= 0.0 || g_outerArmLength <= 0.0) while(true);
  
  /* Inicializar interfaz serial a 115200 bps. */
  Serial.begin(115200);
  while(!Serial);
  
  /* Inicializar servos. */
  servoInitialize(&g_shoulderServoInfo, 2, 500, 2500, 180.0);
  servoInitialize(&g_elbowServoInfo, 3, 500, 2500, 180.0);
  
  /* Establecer servos en la posición base. */
  servoMove(&g_shoulderServoInfo, 180.0);
  servoMove(&g_elbowServoInfo, 180.0);
}

void loop() {
  /* Verificar si hay datos disponibles para leer. */
  if (Serial.available() <= 0)
  {
    delay(100);
    return;
  }
  
  /* Leer datos hasta conseguir un salto de línea. */
  String line = Serial.readStringUntil('\n');
  
  /* Procesar comando G-code. */
  gcodeProcessCommand(line.c_str());
}

void servoInitialize(ServoInfo *servo_info, int pin, int min_pulse_width, int max_pulse_width, double max_angle)
{
  if (!servo_info || pin < 0 || min_pulse_width <= 0 || max_pulse_width <= 0 || max_pulse_width <= min_pulse_width || !isfinite(max_angle) || max_angle <= 0.0) return;
  
  if (servo_info->servo.attached()) servo_info->servo.detach();
  
  servo_info->servo.attach(pin, min_pulse_width, max_pulse_width);
  
  servo_info->pin = pin;
  servo_info->min_pulse_width = min_pulse_width;
  servo_info->max_pulse_width = max_pulse_width;
  servo_info->max_angle = max_angle;
  
  servo_info->multiplier = ((double)(max_pulse_width - min_pulse_width) / max_angle);
}

void servoMove(ServoInfo *servo_info, double deg)
{
  if (!servo_info || !isfinite(deg) || deg < 0.0 || deg > servo_info->max_angle) return;
  
  int usec = (int)((deg * servo_info->multiplier) + (double)servo_info->min_pulse_width);
  servo_info->servo.writeMicroseconds(usec);
  
#ifdef DEBUG_SERVO
  Serial.print("Servo @ pin #");
  Serial.print(servo_info->pin);
  Serial.print(": ");
  Serial.print(deg);
  Serial.print("º (");
  Serial.print(usec);
  Serial.println(" usec).");
#endif
}

void gcodeConvertCoordsToAngles(double x, double y, double *shoulder, double *elbow)
{
  if (!shoulder || !elbow) return;
  
  *shoulder = *elbow = NAN;
  
  /* Calcular distancia entre el hombro (primer motor) y el punto X,Y proporcionado. */
  double d_inner = hypot(x, y);
  
  /* Calcular ángulo entre la línea de distancia y el antebrazo (primer triángulo). */
  double d_inner_angle = calcTriangleVertexAngle(d_inner, g_innerArmLength, g_outerArmLength);
  if (!isfinite(d_inner_angle)) return;
  
  /* Calcular ángulo entre la línea de distancia y la componente vertical (segundo triángulo). */
  /* El resultado devuelto está en el rango [-pi/2, pi/2] radianes. */
  double d_y_angle = atan(x / y);
  if (!isfinite(d_y_angle)) return;
  
  /* Calcular ángulo entre el antebrazo y la componente vertical (primer y segundo triángulo). */
  double inner_y_angle = (fmax(d_inner_angle, fabs(d_y_angle)) - fmin(d_inner_angle, fabs(d_y_angle)));
  
  /* Calcular ángulo del hombro. */
  /* Este valor debe poder usarse con servoMove(). */
  double shoulder_angle = ((rad2deg(inner_y_angle) * (d_y_angle < 0.0 ? -1.0 : 1.0)) + 90.0);
  if (shoulder_angle < 0.0 || shoulder_angle > 180.0) return;
  
  /* Calcular ángulo entre el antebrazo y el brazo (primer triángulo). */
  double inner_outer_angle = calcTriangleVertexAngle(g_innerArmLength, g_outerArmLength, d_inner);
  if (!isfinite(inner_outer_angle)) return;
  
  /* Calcular ángulo del codo (segundo motor). */
  /* Este valor debe poder usarse con servoMove(). */
  double elbow_angle = (180.0 - rad2deg(inner_outer_angle));
  if (elbow_angle < 0.0 || elbow_angle > 180.0) return;
  
#ifdef VERIFY_CONV
  /* Los cálculos a continuación sirven a manera de comprobación. */
  /* d_outer siempre debe ser igual a la longitud del brazo posterior. */
  
  /* Calcular ángulo entre el antebrazo y la componente horizontal (tercer triángulo). */
  double inner_x_angle = deg2rad(map_double(shoulder_angle, 0.0, 180.0, 0.0, 90.0));
  
  /* Calcular posición del codo (segundo motor) en el eje X. */
  double elbow_x = (cos(inner_x_angle) * g_innerArmLength);
  if (shoulder_angle < 90.0) elbow_x = -elbow_x;
  
  /* Calcular posición del codo en el eje Y. */
  double elbow_y = (tan(inner_x_angle) * fabs(elbow_x));
  
  /* Calcular la distancia entre el codo y el punto X,Y proporcionado. */
  double d_outer = hypot(elbow_x - x, elbow_y - y);
  if (round(d_outer) != round(g_outerArmLength)) return;
#endif
  
  /* Invertir ángulos. */
  double shoulder_angle_inv = (180.0 - shoulder_angle);
  double elbow_angle_inv = (180.0 - elbow_angle);
  
  /* Actualizar variables de salida. */
  *shoulder = map_double(shoulder_angle_inv, 0.0, 180.0, 110.0, 180.0);
  *elbow = map_double(elbow_angle_inv, 0.0, 180.0, 130.0, 180.0);
  
#ifdef DEBUG_CONV
  Serial.print("x: ");
  Serial.print(x);
  Serial.print(" mm, y: ");
  Serial.print(y);
  Serial.println(" mm");
  
  Serial.print("d_inner: ");
  Serial.print(d_inner);
  Serial.print(" mm, d_inner_angle: ");
  Serial.print(d_inner_angle);
  Serial.print(" rad, d_y_angle: ");
  Serial.print(d_y_angle);
  Serial.println(" rad");
  
  Serial.print("inner_y_angle: ");
  Serial.print(inner_y_angle);
  Serial.print(" rad, shoulder_angle: ");
  Serial.print(shoulder_angle);
  Serial.println(" deg");
  
  Serial.print("inner_outer_angle: ");
  Serial.print(inner_outer_angle);
  Serial.print(" rad, elbow_angle: ");
  Serial.print(elbow_angle);
  Serial.println(" deg");
  
#ifdef VERIFY_CONV
  Serial.print("inner_x_angle: ");
  Serial.print(inner_x_angle);
  Serial.print(" rad, elbow_x: ");
  Serial.print(elbow_x);
  Serial.print(" mm, elbow_y: ");
  Serial.print(elbow_y);
  Serial.print(" mm, d_outer: ");
  Serial.print(d_outer);
  Serial.println(" mm");
#endif
  
  Serial.print("shoulder_angle_inv: ");
  Serial.print(shoulder_angle_inv);
  Serial.print(" deg, shoulder: ");
  Serial.print(*shoulder);
  Serial.println(" deg");

  Serial.print("elbow_angle_inv: ");
  Serial.print(elbow_angle_inv);
  Serial.print(" deg, elbow: ");
  Serial.print(*elbow);
  Serial.println(" deg");
#endif
}

/* Calcula el ángulo del vértice entre los lados a y b de un triángulo, usando las medidas de sus tres lados. */
/* El resultado devuelto se encuentra en el rango [0, pi] radianes. */
double calcTriangleVertexAngle(double a, double b, double c)
{
  return acos(((a * a) + (b * b) - (c * c)) / (2 * fabs(a) * fabs(b)));
}

/* Convierte una medida en radianes a grados sexagesimales. */
double rad2deg(double value)
{
  return ((value * 180.0) / M_PI);
}

/* Convierte una medida en grados sexagesimales a radianes. */
double deg2rad(double value)
{
  return ((value * M_PI) / 180.0);
}

double map_double(double x, double in_min, double in_max, double out_min, double out_max) {
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

void gcodeProcessCommand(const char *cmd)
{
  if (!cmd || !*cmd) return;
  
  u32 cmd_argc = 0;
  char **cmd_argv = NULL;
  
  char cmd_type = 0;
  u32 cmd_id = 0;
  
  /* Obtener argumentos del comando. */
  cmd_argv = gcodeGetCommandArguments(cmd, &cmd_argc);
  if (!cmd_argv || !cmd_argc || !cmd_argv[0][1]) goto end_func;
  
  /* Obtener tipo de comando e ID de comando. */
  sscanf(cmd_argv[0], "%c%u", &cmd_type, &cmd_id);
  
  /* Procesar comando. */
  if (cmd_type == 'G')
  {
    if (cmd_id == 0 || cmd_id == 1)
    {
      /* G0 y G1: Move. */
      gcodeMove(cmd_argc, (const char**)cmd_argv);
    } else
    if (cmd_id == 90)
    {
      /* G90: Set to Absolute Positioning. */
      g_absPos = true;
#ifdef DEBUG_POS
      Serial.println("Posicionamiento absoluto habilitado.");
#endif
    } else
    if (cmd_id == 91)
    {
      /* G91: Set to Relative Positioning. */
      g_absPos = false;
#ifdef DEBUG_POS
      Serial.println("Posicionamiento absoluto deshabilitado.");
#endif
    }
  }
  
end_func:
  gcodeFreeCommandArguments(&cmd_argv, cmd_argc);
}

void gcodeMove(u32 cmd_argc, const char **cmd_argv)
{
  if (cmd_argc <= 1 || !cmd_argv) return;
  
  double x = 0.0, y = 0.0;
  double shoulder = 0.0, elbow = 0.0;
  
  for(u32 i = 1; i < cmd_argc; i++)
  {
    char arg_type = cmd_argv[i][0];
    if (arg_type != 'X' && arg_type != 'Y') continue;
    
    double *value = (arg_type == 'X' ? &x : &y);
    *value = strtod(cmd_argv[i] + 1, NULL);
  }
  
  /* Sumar coordenadas actuales a las proporcionadas si estamos en modo relativo. */
  if (!g_absPos)
  {
    x += g_posX;
    y += g_posY;
  }
  
  /* Convertir coordenadas a ángulos. */
  gcodeConvertCoordsToAngles(x, y, &shoulder, &elbow);
  
  /* Verificar si los ángulos generados están dentro del rango. */
  if (!isfinite(shoulder) || !isfinite(elbow))
  {
#ifdef DEBUG_MOVE
    Serial.print("Punto ");
    Serial.print(x);
    Serial.print(",");
    Serial.print(y);
    Serial.println(" fuera de rango.");
#endif
    return;
  }
  
  /* Mover brazo robótico. */
  servoMove(&g_shoulderServoInfo, shoulder);
  servoMove(&g_elbowServoInfo, elbow);
  
  /* Actualizar coordenadas actuales. */
  g_posX = x;
  g_posY = y;
  
#ifdef DEBUG_MOVE
  Serial.print("X: ");
  Serial.print(g_posX);
  Serial.print(", Y: ");
  Serial.print(g_posY);
  Serial.print(", shoulder: ");
  Serial.print(shoulder);
  Serial.print(", elbow: ");
  Serial.println(elbow);
#endif
}

char **gcodeGetCommandArguments(const char *cmd, u32 *cmd_argc)
{
  if (!cmd || !*cmd || !cmd_argc) return NULL;

  char *cmd_dup = NULL, *pch = NULL, **cmd_argv = NULL, **tmp_cmd_argv = NULL;
  u32 argc = 0;
  bool success = false;
  
  cmd_dup = strdup(cmd);
  if (!cmd_dup) goto end_func;
  
  pch = strtok(cmd_dup, " ;\r\n");
  while(pch)
  {
    tmp_cmd_argv = (char**)realloc(cmd_argv, (argc + 1) * sizeof(char*));
    if (!tmp_cmd_argv) goto end_func;
    
    cmd_argv = tmp_cmd_argv;
    tmp_cmd_argv = NULL;
    
    cmd_argv[argc] = strdup(pch);
    if (!cmd_argv[argc]) goto end_func;
    
    argc++;
    
    pch = strtok(NULL, " ;\r\n");
  }
  
  if (argc > 0)
  {
    *cmd_argc = argc;
    success = true;
  }
  
end_func:
  if (!success) gcodeFreeCommandArguments(&cmd_argv, argc);
  
  if (cmd_dup) free(cmd_dup);
  
  return cmd_argv;
}

void gcodeFreeCommandArguments(char ***cmd_argv, u32 cmd_argc)
{
  if (!cmd_argv || !*cmd_argv) return;
  
  char **cmd_argv_proc = *cmd_argv;
  
  for(u32 i = 0; i < cmd_argc; i++)
  {
    if (cmd_argv_proc[i]) free(cmd_argv_proc[i]);
  }
  
  free(cmd_argv_proc);
  
  *cmd_argv = NULL;
}
