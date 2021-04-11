


/// Contexto que describe parámetros Denavit-Hartenberg para una articulación.
typedef struct {
    double arr[4][4];   ///< Matriz de transformación Denavit-Hartenberg. No se calcula para la articulación base.
    double d;           ///< Desplazamiento en el eje Z.
    double theta;       ///< Ángulo de diferencia entre el eje X anterior y el nuevo.
    double r;           ///< Separación entre la articulación anterior y la actual.
    double alpha;       ///< Ángulo de diferencia entre el eje Z anterior y el nuevo.
} DenavitHartenbergContext;

void dhInitializeContext(DenavitHartenbergContext *dh_ctx, double d, double theta, double r, double alpha)
{
    if (!dh_ctx) return;
    
    dh_ctx->d = d;
    dh_ctx->theta = theta;
    dh_ctx->r = r;
    dh_ctx->alpha = alpha;
}

void dhGenerateTransformationMatrix(DenavitHartenbergContext *cur_dh_ctx, DenavitHartenbergContext *prev_dh_ctx)
{
    if (!cur_dh_ctx || !prev_dh_ctx) return;
    
    double ***arr = &(cur_dh_ctx->arr);
    
    double d = prev_dh_ctx->d;
    double theta = prev_dh_ctx->theta;
    double r = cur_dh_ctx->r;
    double alpha = cur_dh_ctx->alpha;
    
    (*arr)[0][0] = cos(theta);
    (*arr)[0][1] = (-sin(theta) * cos(alpha));
    (*arr)[0][2] = (sin(theta) * cos(alpha));
    (*arr)[0][3] = (r * cos(theta));
    
    (*arr)[1][0] = sin(theta);
    (*arr)[1][1] = (cos(theta) * cos(alpha));
    (*arr)[1][2] = (-cos(theta) * sin(alpha));
    (*arr)[1][3] = (r * sin(theta));
    
    (*arr)[2][0] = 0;
    (*arr)[2][1] = sin(alpha);
    (*arr)[2][2] = cos(alpha);
    (*arr)[2][3] = d;
    
    (*arr)[3][0] = 0;
    (*arr)[3][1] = 0;
    (*arr)[3][2] = 0;
    (*arr)[3][3] = 1;
}






