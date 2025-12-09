/* Configuration des modules FreeType pour ALOS
 * 
 * Modules minimaux nécessaires pour le rendu TTF de base.
 */

#ifndef FTMODULE_H_
#define FTMODULE_H_

FT_USE_MODULE( FT_Module_Class, autofit_module_class )
FT_USE_MODULE( FT_Driver_ClassRec, tt_driver_class )
FT_USE_MODULE( FT_Module_Class, psnames_module_class )
FT_USE_MODULE( FT_Renderer_Class, ft_smooth_renderer_class )

#endif /* FTMODULE_H_ */

