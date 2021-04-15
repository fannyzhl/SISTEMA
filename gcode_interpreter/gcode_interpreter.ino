#include <Servo.h>

typedef struct
{
  Servo *servo;
  int pin;
  int min_pulse_width;
  int max_pulse_width;
  double multiplier;
} ServoInfo;

const double g_innerArmLength = 80.0, g_outerArmLength = 80.0; /* Expresado en mm. */

double g_posX = 0.0, g_posY = 0.0;
bool g_absPos = false;

Servo g_shoulderServo, g_elbowServo;
ServoInfo g_shoulderServoInfo, g_elbowServoInfo;

void setup()
{
  /* Validar ángulos. No hacer nada si representan valores inválidos. */
  if (g_innerArmLength <= 0.0 || g_outerArmLength <= 0.0)
    while (true)
      ;

  /* Inicializar interfaz serial a 115200 bps. */
  Serial.begin(115200);
  while (!Serial)
    ;

  /* Inicializar servos. */
  servoInitialize(&g_shoulderServoInfo, &g_shoulderServo, 2, 600, 2400);
  servoInitialize(&g_elbowServoInfo, &g_elbowServo, 3, 600, 2400);

  /* Establecer servos en la posición base. */
  char gcode_cmd[32] = {0};
  sprintf(gcode_cmd, "G1 X%.2f Y%.2f;", g_innerArmLength, g_outerArmLength);
  gcodeProcessCommand(gcode_cmd);
}

void loop()
{
  /* Verificar si hay datos disponibles para leer. */
  if (Serial.available() > 0)
  {
    /* Leer datos hasta conseguir un salto de línea. */
    String line = Serial.readStringUntil('\n');

    /* Procesar comando G-code. */
    gcodeProcessCommand(line.c_str());
  }

  delay(100);
}

void servoInitialize(ServoInfo *servo_info, Servo *servo, int pin, int min_pulse_width, int max_pulse_width)
{
  if (!servo_info || !servo)
    return;

  memset(servo_info, 0, sizeof(ServoInfo));

  servo_info->servo = servo;
  servo_info->servo->attach(pin, min_pulse_width, max_pulse_width);

  servo_info->pin = pin;
  servo_info->min_pulse_width = min_pulse_width;
  servo_info->max_pulse_width = min_pulse_width;

  servo_info->multiplier = ((double)(max_pulse_width - min_pulse_width) / 180.0);
}

void servoMove(ServoInfo *servo_info, double angle)
{
  if (!servo_info || !servo_info->servo)
    return;
  int usec = (int)((angle * servo_info->multiplier) + (double)servo_info->min_pulse_width);
  servo_info->servo->writeMicroseconds(usec);
}

void gcodeConvertCoordsToAngles(double x, double y, double *shoulder, double *elbow)
{
  if (!shoulder || !elbow)
    return;

  *shoulder = *elbow = NAN;

  /* Calcular distancia entre el hombro (primer motor) y el punto X,Y proporcionado. */
  double d_inner = hypot(x, y);

  /* Calcular ángulo entre la línea de distancia y el antebrazo (primer triángulo). */
  double d_inner_angle = calcTriangleVertexAngle(d_inner, g_innerArmLength, g_outerArmLength);
  if (isnan(d_inner_angle))
    return;

  /* Calcular ángulo entre la línea de distancia y la componente vertical (segundo triángulo). */
  double d_y_angle = atan(y / x);
  if (isnan(d_y_angle))
    return;

  /* Calcular ángulo del hombro. */
  /* Este valor debe poder usarse con servoMove(). */
  double shoulder_angle = rad2deg(d_y_angle + d_inner_angle);

  /* Calcular ángulo entre el antebrazo y el brazo (primer triángulo). */
  double inner_outer_angle = calcTriangleVertexAngle(g_innerArmLength, g_outerArmLength, d_inner);
  if (isnan(inner_outer_angle))
    return;

  /* Calcular ángulo del codo (segundo motor). */
  /* Este valor debe poder usarse con servoMove(). */
  double elbow_angle = (shoulder_angle + rad2deg(inner_outer_angle) - 180.0);

  /* Actualizar variables de salida. */
  *shoulder = map(shoulder_angle, 125, 90, 0, 35);
  *elbow = map(elbow_angle, 0, 20, 190, 210);

  /*Serial.print("x: ");
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

  Serial.print("inner_x_angle: ");
  Serial.print(inner_x_angle);
  Serial.print(" rad, elbow_x: ");
  Serial.print(elbow_x);
  Serial.print(" mm, elbow_y: ");
  Serial.print(elbow_y);
  Serial.print(" mm, d_outer: ");
  Serial.print(d_outer);
  Serial.println(" mm");*/
}

/* Calcula el ángulo del vértice entre los lados a y b de un triángulo, usando las medidas de sus tres lados. */
/* El resultado devuelto está expresado en radianes. */
double calcTriangleVertexAngle(double a, double b, double c)
{
  return acos((pow(a, 2.0) + pow(b, 2.0) - pow(c, 2.0)) / (2 * fabs(a) * fabs(b)));
}

/* Convierte una medida en radianes a grados sexagesimales. */
double rad2deg(double val)
{
  return ((val * 180.0) / M_PI);
}

/* Convierte una medida en grados sexagesimales a radianes. */
double deg2rad(double val)
{
  return ((val * M_PI) / 180.0);
}

void gcodeProcessCommand(const char *cmd)
{
  if (!cmd || !*cmd)
    return;

  u32 cmd_argc = 0;
  char **cmd_argv = NULL;

  char cmd_type = 0;
  u32 cmd_id = 0;

  /* Obtener argumentos del comando. */
  cmd_argv = gcodeGetCommandArguments(cmd, &cmd_argc);
  if (!cmd_argv || !cmd_argc || !cmd_argv[0][1])
    goto end;

  /* Obtener tipo de comando e ID de comando. */
  sscanf(cmd_argv[0], "%c%u", &cmd_type, &cmd_id);

  /* Procesar comando. */
  if (cmd_type == 'G')
  {
    if (cmd_id == 0 || cmd_id == 1)
    {
      /* G0 y G1: Move. */
      gcodeMove(cmd_argc, (const char **)cmd_argv);
    }
    else if (cmd_id == 90)
    {
      /* G90: Set to Absolute Positioning. */
      g_absPos = true;
    }
    else if (cmd_id == 91)
    {
      /* G91: Set to Relative Positioning. */
      g_absPos = false;
    }
  }

end:
  gcodeFreeCommandArguments(cmd_argv, cmd_argc);
}

void gcodeMove(u32 cmd_argc, const char **cmd_argv)
{
  if (cmd_argc <= 1 || !cmd_argv)
    return;

  double x = 0.0, y = 0.0;
  double shoulder = 0.0, elbow = 0.0;

  for (u32 i = 1; i < cmd_argc; i++)
  {
    char arg_type = cmd_argv[i][0];
    if (arg_type != 'X' && arg_type != 'Y')
      continue;

    double *val = (arg_type == 'X' ? &x : &y);
    *val = strtod(cmd_argv[i] + 1, NULL);
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
  if (isnan(shoulder) || isnan(elbow))
    return;

  /* Mover brazo robótico. */
  servoMove(&g_shoulderServoInfo, shoulder);
  servoMove(&g_elbowServoInfo, elbow);

  /* Actualizar coordenadas actuales. */
  g_posX = x;
  g_posY = y;

  /*Serial.write("X: ");
  Serial.print(g_posX);
  Serial.write(", Y: ");
  Serial.print(g_posY);
  Serial.write(", shoulder: ");
  Serial.print(shoulder);
  Serial.write(", elbow: ");
  Serial.println(elbow);*/
}

char **gcodeGetCommandArguments(const char *cmd, u32 *cmd_argc)
{
  if (!cmd || !*cmd || !cmd_argc)
    return NULL;

  char *cmd_dup = NULL, *pch = NULL, **cmd_argv = NULL, **tmp_cmd_argv = NULL;
  u32 argc = 0;
  bool success = false;

  cmd_dup = strdup(cmd);
  if (!cmd_dup)
    goto end;

  pch = strtok(cmd_dup, " ;\r\n");
  while (pch)
  {
    tmp_cmd_argv = (char **)realloc(cmd_argv, (argc + 1) * sizeof(char *));
    if (!tmp_cmd_argv)
      goto end;

    cmd_argv = tmp_cmd_argv;
    tmp_cmd_argv = NULL;

    cmd_argv[argc] = strdup(pch);
    if (!cmd_argv[argc])
      goto end;

    argc++;

    pch = strtok(NULL, " ;\r\n");
  }

  if (argc > 0)
  {
    *cmd_argc = argc;
    success = true;
  }

end:
  if (!success)
    gcodeFreeCommandArguments(cmd_argv, argc);

  if (cmd_dup)
    free(cmd_dup);

  return cmd_argv;
}

void gcodeFreeCommandArguments(char **cmd_argv, u32 cmd_argc)
{
  if (!cmd_argv)
    return;

  for (u32 i = 0; i < cmd_argc; i++)
  {
    if (cmd_argv[i])
      free(cmd_argv[i]);
  }

  free(cmd_argv);
}
