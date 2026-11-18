#pragma once

// "Invítame un café": un mensaje breve con el enlace a Ko-fi. Solo se abre cuando el usuario lo
// pide (el botón al final de las Propiedades de la fuente); nunca aparece solo y no guarda nada.

class QWidget;

void open_support_dialog(QWidget *parent);
