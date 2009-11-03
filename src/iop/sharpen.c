#ifdef HAVE_CONFIG_H
  #include "config.h"
#endif
#include <stdlib.h>
#include <math.h>
#include <assert.h>
#include <string.h>
#ifdef HAVE_GEGL
  #include <gegl.h>
#endif
#include "develop/develop.h"
#include "develop/imageop.h"
#include "control/control.h"
#include "gui/gtk.h"
#include <gtk/gtk.h>
#include <inttypes.h>

#define MAXR 8

typedef struct dt_iop_sharpen_params_t
{
  float radius, amount, threshold;
}
dt_iop_sharpen_params_t;

typedef struct dt_iop_sharpen_gui_data_t
{
  GtkVBox   *vbox1,  *vbox2;
  GtkLabel  *label1, *label2, *label3;
  GtkHScale *scale1, *scale2, *scale3;
}
dt_iop_sharpen_gui_data_t;

typedef struct dt_iop_sharpen_data_t
{
  float radius, amount, threshold;
}
dt_iop_sharpen_data_t;

#if 0
void modify_roi_out(struct dt_iop_module_t *self, struct dt_dev_pixelpipe_iop_t *piece, dt_iop_roi_t *roi_out, const dt_iop_roi_t *roi_in)
{
  *roi_out = *roi_in;
  // roi_out->x      = roi_in->x + (int)(MAXR/piece->iscale);
  // roi_out->y      = roi_in->y + (int)(MAXR/piece->iscale);
  roi_out->width  = roi_in->width  - 2*(int)(MAXR/piece->iscale);
  roi_out->height = roi_in->height - 2*(int)(MAXR/piece->iscale);
  printf("mod roiout : xy: %d %d => %d %d\n", roi_in->x, roi_in->y, roi_out->x, roi_out->y);
}

void modify_roi_in(struct dt_iop_module_t *self, struct dt_dev_pixelpipe_iop_t *piece, const dt_iop_roi_t *roi_out, dt_iop_roi_t *roi_in)
{
  *roi_in = *roi_out;
  roi_in->width  = roi_out->width  + 2*(int)(MAXR/piece->iscale);
  roi_in->height = roi_out->height + 2*(int)(MAXR/piece->iscale);
  printf("mod roiin : xy: %d %d <= %d %d\n", roi_in->x, roi_in->y, roi_out->x, roi_out->y);
}
#endif

const char *name()
{
  return _("sharpen");
}

void process (struct dt_iop_module_t *self, dt_dev_pixelpipe_iop_t *piece, void *ivoid, void *ovoid, const dt_iop_roi_t *roi_in, const dt_iop_roi_t *roi_out)
{
  dt_iop_sharpen_data_t *data = (dt_iop_sharpen_data_t *)piece->data;
  float *in  = (float *)ivoid;
  float *out = (float *)ovoid;

  const int rad = data->radius * roi_in->scale / (piece->iscale * piece->iscale);
  if(rad == 0)
  {
    memcpy(out, in, sizeof(float)*3*roi_out->width*roi_out->height);
    return;
  }
  float mat[2*(MAXR+1)*2*(MAXR+1)];
  const int wd = 2*rad+1;
  float *m = mat + rad*wd + rad;
  const float sigma2 = (2.5*2.5)*(data->radius*roi_in->scale)*(data->radius*roi_in->scale);
  float weight = 0.0f;
  // init gaussian kernel
  for(int l=-rad;l<=rad;l++) for(int k=-rad;k<=rad;k++)
    weight += m[l*wd + k] = expf(- (l*l + k*k)/(2.f*sigma2));
  for(int l=-rad;l<=rad;l++) for(int k=-rad;k<=rad;k++)
    m[l*wd + k] /= weight;

  // gauss blur the image
  for(int j=rad;j<roi_out->height-rad;j++)
  {
    in  = ((float *)ivoid) + 3*((rad+j)*roi_in->width + rad);
    out = ((float *)ovoid) + 3*(j*roi_out->width + rad);
    for(int i=rad;i<roi_out->width-rad;i++)
    {
      for(int c=0;c<3;c++) out[c] = 0.0f;
      for(int l=-rad;l<=rad;l++) for(int k=-rad;k<=rad;k++)
        for(int c=0;c<3;c++) out[c] += m[l*wd+k]*in[3*(l*roi_in->width+k)+c];
      out += 3; in += 3;
    }
  }
  in  = (float *)ivoid;
  out = (float *)ovoid;

#if 1 // fill unsharpened border
  for(int j=0;j<rad;j++)
    memcpy(((float*)ovoid) + 3*j*roi_out->width, ((float*)ivoid) + 3*j*roi_in->width, 3*sizeof(float)*roi_out->width);
  for(int j=roi_out->height-rad;j<roi_out->height;j++)
    memcpy(((float*)ovoid) + 3*j*roi_out->width, ((float*)ivoid) + 3*j*roi_in->width, 3*sizeof(float)*roi_out->width);
  for(int j=rad;j<roi_out->height-rad;j++)
  {
    for(int i=0;i<rad;i++)
      for(int c=0;c<3;c++) out[3*(roi_out->width*j + i) + c] = in[3*(roi_in->width*j + i) + c];
    for(int i=roi_out->width-rad;i<roi_out->width;i++)
      for(int c=0;c<3;c++) out[3*(roi_out->width*j + i) + c] = in[3*(roi_in->width*j + i) + c];
  }
#endif

  // subtract blurred image, if diff > thrs, add *amount to orginal image
  for(int j=0;j<roi_out->height;j++) for(int i=0;i<roi_out->width;i++)
  {
    for(int c=0;c<3;c++)
    {
      const float diff = in[c] - out[c];
      if(fabsf(diff) > data->threshold)
      {
        const float detail = copysignf(fmaxf(fabsf(diff) - data->threshold, 0.0), diff);
        out[c] = fmaxf(0.0, in[c] + detail*data->amount);
      }
      else out[c] = in[c];
    }
    out += 3; in += 3;
  }
}

void radius_callback (GtkRange *range, gpointer user_data)
{
  dt_iop_module_t *self = (dt_iop_module_t *)user_data;
  if(self->dt->gui->reset) return;
  dt_iop_sharpen_params_t *p = (dt_iop_sharpen_params_t *)self->params;
  p->radius = gtk_range_get_value(range);
  dt_dev_add_history_item(darktable.develop, self);
}

void amount_callback (GtkRange *range, gpointer user_data)
{
  dt_iop_module_t *self = (dt_iop_module_t *)user_data;
  if(self->dt->gui->reset) return;
  dt_iop_sharpen_params_t *p = (dt_iop_sharpen_params_t *)self->params;
  p->amount = gtk_range_get_value(range);
  dt_dev_add_history_item(darktable.develop, self);
}

void threshold_callback (GtkRange *range, gpointer user_data)
{
  dt_iop_module_t *self = (dt_iop_module_t *)user_data;
  if(self->dt->gui->reset) return;
  dt_iop_sharpen_params_t *p = (dt_iop_sharpen_params_t *)self->params;
  p->threshold = gtk_range_get_value(range);
  dt_dev_add_history_item(darktable.develop, self);
}

void commit_params (struct dt_iop_module_t *self, dt_iop_params_t *p1, dt_dev_pixelpipe_t *pipe, dt_dev_pixelpipe_iop_t *piece)
{
  dt_iop_sharpen_params_t *p = (dt_iop_sharpen_params_t *)p1;
#ifdef HAVE_GEGL
  fprintf(stderr, "[sharpen] TODO: implement gegl version!\n");
  // pull in new params to gegl
#else
  dt_iop_sharpen_data_t *d = (dt_iop_sharpen_data_t *)piece->data;
  d->radius = p->radius;
  d->amount = p->amount;
  d->threshold = p->threshold;
#endif
}

void init_pipe (struct dt_iop_module_t *self, dt_dev_pixelpipe_t *pipe, dt_dev_pixelpipe_iop_t *piece)
{
#ifdef HAVE_GEGL
  // create part of the gegl pipeline
  piece->data = NULL;
#else
  piece->data = malloc(sizeof(dt_iop_sharpen_data_t));
  self->commit_params(self, self->default_params, pipe, piece);
#endif
}

void cleanup_pipe (struct dt_iop_module_t *self, dt_dev_pixelpipe_t *pipe, dt_dev_pixelpipe_iop_t *piece)
{
#ifdef HAVE_GEGL
  // clean up everything again.
  (void)gegl_node_remove_child(pipe->gegl, piece->input);
  // no free necessary, no data is alloc'ed
#else
  free(piece->data);
#endif
}

void gui_update(struct dt_iop_module_t *self)
{
  dt_iop_module_t *module = (dt_iop_module_t *)self;
  dt_iop_sharpen_gui_data_t *g = (dt_iop_sharpen_gui_data_t *)self->gui_data;
  dt_iop_sharpen_params_t *p = (dt_iop_sharpen_params_t *)module->params;
  gtk_range_set_value(GTK_RANGE(g->scale1), p->radius);
  gtk_range_set_value(GTK_RANGE(g->scale2), p->amount);
  gtk_range_set_value(GTK_RANGE(g->scale3), p->threshold);
}

void init(dt_iop_module_t *module)
{
  // module->data = malloc(sizeof(dt_iop_sharpen_data_t));
  module->params = malloc(sizeof(dt_iop_sharpen_params_t));
  module->default_params = malloc(sizeof(dt_iop_sharpen_params_t));
  module->default_enabled = 0;
  module->priority = 97;
  module->params_size = sizeof(dt_iop_sharpen_params_t);
  module->gui_data = NULL;
  dt_iop_sharpen_params_t tmp = (dt_iop_sharpen_params_t){1.0, 1.0, 0.0};
  memcpy(module->params, &tmp, sizeof(dt_iop_sharpen_params_t));
  memcpy(module->default_params, &tmp, sizeof(dt_iop_sharpen_params_t));
}

void cleanup(dt_iop_module_t *module)
{
  free(module->gui_data);
  module->gui_data = NULL;
  free(module->params);
  module->params = NULL;
}

void gui_init(struct dt_iop_module_t *self)
{
  self->gui_data = malloc(sizeof(dt_iop_sharpen_gui_data_t));
  dt_iop_sharpen_gui_data_t *g = (dt_iop_sharpen_gui_data_t *)self->gui_data;
  dt_iop_sharpen_params_t *p = (dt_iop_sharpen_params_t *)self->params;

  self->widget = GTK_WIDGET(gtk_hbox_new(FALSE, 0));
  g->vbox1 = GTK_VBOX(gtk_vbox_new(FALSE, 0));
  g->vbox2 = GTK_VBOX(gtk_vbox_new(FALSE, 0));
  gtk_box_pack_start(GTK_BOX(self->widget), GTK_WIDGET(g->vbox1), FALSE, FALSE, 5);
  gtk_box_pack_start(GTK_BOX(self->widget), GTK_WIDGET(g->vbox2), TRUE, TRUE, 5);
  g->label1 = GTK_LABEL(gtk_label_new(_("radius")));
  g->label2 = GTK_LABEL(gtk_label_new(_("amount")));
  g->label3 = GTK_LABEL(gtk_label_new(_("threshold")));
  gtk_misc_set_alignment(GTK_MISC(g->label1), 0.0, 0.5);
  gtk_misc_set_alignment(GTK_MISC(g->label2), 0.0, 0.5);
  gtk_misc_set_alignment(GTK_MISC(g->label3), 0.0, 0.5);
  gtk_box_pack_start(GTK_BOX(g->vbox1), GTK_WIDGET(g->label1), TRUE, TRUE, 0);
  gtk_box_pack_start(GTK_BOX(g->vbox1), GTK_WIDGET(g->label2), TRUE, TRUE, 0);
  gtk_box_pack_start(GTK_BOX(g->vbox1), GTK_WIDGET(g->label3), TRUE, TRUE, 0);
  g->scale1 = GTK_HSCALE(gtk_hscale_new_with_range(0.0, 8.0000, 0.001));
  g->scale2 = GTK_HSCALE(gtk_hscale_new_with_range(0.0, 2.0000, 0.001));
  g->scale3 = GTK_HSCALE(gtk_hscale_new_with_range(0.0, 1.0000, 0.001));
  gtk_scale_set_digits(GTK_SCALE(g->scale1), 3);
  gtk_scale_set_digits(GTK_SCALE(g->scale2), 3);
  gtk_scale_set_digits(GTK_SCALE(g->scale3), 3);
  gtk_scale_set_value_pos(GTK_SCALE(g->scale1), GTK_POS_LEFT);
  gtk_scale_set_value_pos(GTK_SCALE(g->scale2), GTK_POS_LEFT);
  gtk_scale_set_value_pos(GTK_SCALE(g->scale3), GTK_POS_LEFT);
  gtk_range_set_value(GTK_RANGE(g->scale1), p->radius);
  gtk_range_set_value(GTK_RANGE(g->scale2), p->amount);
  gtk_range_set_value(GTK_RANGE(g->scale3), p->threshold);
  gtk_box_pack_start(GTK_BOX(g->vbox2), GTK_WIDGET(g->scale1), TRUE, TRUE, 0);
  gtk_box_pack_start(GTK_BOX(g->vbox2), GTK_WIDGET(g->scale2), TRUE, TRUE, 0);
  gtk_box_pack_start(GTK_BOX(g->vbox2), GTK_WIDGET(g->scale3), TRUE, TRUE, 0);

  g_signal_connect (G_OBJECT (g->scale1), "value-changed",
                    G_CALLBACK (radius_callback), self);
  g_signal_connect (G_OBJECT (g->scale2), "value-changed",
                    G_CALLBACK (amount_callback), self);
  g_signal_connect (G_OBJECT (g->scale3), "value-changed",
                    G_CALLBACK (threshold_callback), self);
}

void gui_cleanup(struct dt_iop_module_t *self)
{
  free(self->gui_data);
  self->gui_data = NULL;
}

#undef MAXR
