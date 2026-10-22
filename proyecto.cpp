/**
 * ============================================================================
 * PROYECTO INTEGRADOR - LENGUAJES FORMALES Y AUTÓMATAS
 * OPCIÓN B: Simulador de Autómata a Pila Determinístico (APD) para MiniHTML
 * Estándar: C++17
 * ============================================================================
 *
 * DEFINICIÓN FORMAL DEL AUTÓMATA A PILA:
 * M = (Q, Sigma, Gamma, delta, q0, Z0, F)
 *   - Q     = { q0, qf }
 *   - Sigma = { "<b>", "<i>", "<p>", "</b>", "</i>", "</p>", "TEXTO" }
 *             (soporta también variantes con barra invertida: "<\b>", etc.)
 *   - Gamma = { "Z0", "B", "I", "P" }
 *   - q0    = Estado inicial de procesamiento
 *   - Z0    = Símbolo inicial de la pila (marcador de fondo)
 *   - F     = { qf } (Aceptación por estado final tras vaciar entrada con Z0)
 *
 * CONVENCIÓN DE CONFIGURACIONES INSTANTÁNEAS (ID):
 *   (q, w, gamma) donde el TOPE de la pila se escribe a la IZQUIERDA de gamma.
 * ============================================================================
 */

#include <iostream>
#include <string>
#include <vector>
#include <stack>
#include <unordered_map>
#include <unordered_set>
#include <sstream>
#include <iomanip>
#include <cctype>

// ============================================================================
// [ZONA DE MODIFICACIÓN EN VIVO EN DEFENSA ORAL]
// ============================================================================
// Instrucciones rápidas para responder pedidos del tribunal en 1 línea:
//
// a) AGREGAR NUEVA ETIQUETA CON CIERRE (ej. <div> y </div>):
//    Agregá {"<div>", "DIV"} en APERTURA_A_PILA y
//    {"</div>", {"DIV", "<div>"}} en CIERRE_A_PILA.
//
// b) AGREGAR ETIQUETA AUTOCERRADA / SIN CIERRE (ej. <br> o <hr>):
//    Agregá "<br>" en el conjunto ETIQUETAS_AUTOCERRADAS.
//    Transición formal que aplica: delta(q0, <br>, X) = {(q0, X)} (no altera pila).
//
// c) PROHIBIR QUE 'TEXTO' APAREZCA FUERA DE ETIQUETAS (pila en Z0):
//    Cambiá la constante PROHIBIR_TEXTO_EN_RAIZ a `true`.
//
// d) PROHIBIR QUE UNA ETIQUETA SE ANIDE DENTRO DE SÍ MISMA (ej. <p> dentro de <p>):
//    Agregá el símbolo de pila (ej. "P") en PROHIBIR_AUTOANIDAMIENTO.
//
// e) EXTRA DEFENSA - PROHIBIR CUALQUIER ANIDAMIENTO REPETIDO (ej. <b>...<b>):
//    Cambiá PROHIBIR_REPETICION_EN_TODA_LA_PILA a `true`.
// ============================================================================

struct InfoCierre {
    std::string simboloPilaEsperado; // Símbolo en Gamma que debe estar en el tope
    std::string etiquetaApertura;    // Nombre legible para reportar errores claros
};

// 1. Mapa de etiquetas de apertura: Etiqueta en Sigma -> Símbolo en Gamma
const std::unordered_map<std::string, std::string> APERTURA_A_PILA = {
    {"<b>", "B"},
    {"<i>", "I"},
    {"<p>", "P"}
    // Ejemplo en vivo (a): , {"<div>", "DIV"}
};

// 2. Mapa de etiquetas de cierre: Etiqueta en Sigma -> {Símbolo Gamma, Apertura}
const std::unordered_map<std::string, InfoCierre> CIERRE_A_PILA = {
    {"</b>",  {"B", "<b>"}},
    {"</i>",  {"I", "<i>"}},
    {"</p>",  {"P", "<p>"}},
    // Soporte opcional para barra invertida según consigna:
    {"<\\b>", {"B", "<b>"}},
    {"<\\i>", {"I", "<i>"}},
    {"<\\p>", {"P", "<p>"}}
    // Ejemplo en vivo (a): , {"</div>", {"DIV", "<div>"}}
};

// 3. Conjunto de etiquetas autocerradas (no apilan ni desapilan)
const std::unordered_set<std::string> ETIQUETAS_AUTOCERRADAS = {
    // Ejemplo en vivo (b): "<br>", "<hr>"
};

// 4. Símbolos de pila que NO pueden anidarse inmediatamente dentro de sí mismos
const std::unordered_set<std::string> PROHIBIR_AUTOANIDAMIENTO = {
    // Ejemplo en vivo (d): "P"
};

// 5. Interruptores lógicos de comportamiento para preguntas del docente
const bool PROHIBIR_TEXTO_EN_RAIZ             = false; // (c) true = exige etiqueta abierta antes de TEXTO
const bool PROHIBIR_REPETICION_EN_TODA_LA_PILA = false; // true = prohíbe <p><b><p> (revisa toda la pila)
const bool ACEPTAR_CADENA_VACIA               = true;  // false = rechaza lambda (cadena vacía)
const bool AGRUPAR_PALABRAS_EN_UN_SOLO_TEXTO  = true;  // true = "Hola mundo" -> un solo token TEXTO

// ============================================================================
// FIN DE ZONA DE MODIFICACIÓN EN VIVO
// ============================================================================

// Estructura para el resultado del análisis léxico (Tokenizador)
struct ResultadoLexico {
    bool exito = true;
    std::vector<std::string> tokens;
    std::string tokenInvalido;
    std::string detalleError;
    int posicionCaracter = -1;
};

/**
 * Función auxiliar: recorta espacios en blanco al inicio y al final
 */
std::string trim(const std::string& str) {
    size_t inicio = str.find_first_not_of(" \t\n\r");
    if (inicio == std::string::npos) return "";
    size_t fin = str.find_last_not_of(" \t\n\r");
    return str.substr(inicio, fin - inicio + 1);
}

/**
 * Verifica si una etiqueta pertenece al alfabeto Sigma definido arriba
 */
bool esEtiquetaValidaEnSigma(const std::string& tag) {
    return APERTURA_A_PILA.count(tag) > 0 ||
           CIERRE_A_PILA.count(tag) > 0 ||
           ETIQUETAS_AUTOCERRADAS.count(tag) > 0;
}

/**
 * TOKENIZADOR FLEXIBLE (LEXER)
 * Separa etiquetas <...> y convierte texto plano en el token "TEXTO".
 * Detecta etiquetas mal formadas (ej. "<p", "p>", "<p<b>") o desconocidas ("<u>").
 */
ResultadoLexico tokenizar(const std::string& entrada) {
    ResultadoLexico res;
    size_t i = 0;
    const size_t n = entrada.length();

    while (i < n) {
        // Ignorar espacios en blanco fuera de etiquetas
        if (std::isspace(static_cast<unsigned char>(entrada[i]))) {
            i++;
            continue;
        }

        // Caso 1: Inicio de una etiqueta '<'
        if (entrada[i] == '<') {
            size_t inicioTag = i;
            i++; // Avanzar después de '<'

            while (i < n && entrada[i] != '>' && entrada[i] != '<') {
                i++;
            }

            // Si terminó la cadena o apareció otro '<' antes de cerrar con '>'
            if (i == n || entrada[i] == '<') {
                res.exito = false;
                res.tokenInvalido = entrada.substr(inicioTag, i - inicioTag);
                res.posicionCaracter = static_cast<int>(inicioTag);
                res.detalleError = "Etiqueta mal formada (falta el caracter de cierre '>').";
                return res;
            }

            // Incluir el '>'
            i++;
            std::string tag = entrada.substr(inicioTag, i - inicioTag);

            // Validar si la etiqueta pertenece al alfabeto Sigma
            if (!esEtiquetaValidaEnSigma(tag)) {
                res.exito = false;
                res.tokenInvalido = tag;
                res.posicionCaracter = static_cast<int>(inicioTag);
                res.detalleError = "La etiqueta no pertenece al alfabeto de entrada Sigma.";
                return res;
            }

            res.tokens.push_back(tag);
        }
        // Caso 2: Caracter '>' suelto sin '<' previo (error léxico)
        else if (entrada[i] == '>') {
            res.exito = false;
            res.tokenInvalido = ">";
            res.posicionCaracter = static_cast<int>(i);
            res.detalleError = "Caracter de cierre '>' inesperado fuera de una etiqueta.";
            return res;
        }
        // Caso 3: Texto libre o palabra "TEXTO"
        else {
            size_t inicioTexto = i;
            while (i < n && entrada[i] != '<' && entrada[i] != '>') {
                if (!AGRUPAR_PALABRAS_EN_UN_SOLO_TEXTO &&
                    std::isspace(static_cast<unsigned char>(entrada[i]))) {
                    break;
                }
                i++;
            }

            // Si en medio del texto había un '>' pegado (ej. "hola>"), lo atrapa la siguiente iteración
            std::string fragmento = trim(entrada.substr(inicioTexto, i - inicioTexto));
            if (!fragmento.empty()) {
                // Cualquier cadena de texto plano o la palabra reservada "TEXTO"
                // se tokeniza uniformemente como el terminal "TEXTO" de Sigma.
                res.tokens.push_back("TEXTO");
            }
        }
    }

    return res;
}

/**
 * Convierte el contenido de std::stack<std::string> a una cadena legible
 * con el TOPE A LA IZQUIERDA, respetando la notación formal (q, w, gamma).
 */
std::string obtenerContenidoPilaFormal(std::stack<std::string> copiaPila) {
    if (copiaPila.empty()) return "lambda";
    std::string resultado;
    while (!copiaPila.empty()) {
        resultado += copiaPila.top();
        copiaPila.pop();
        if (!copiaPila.empty()) resultado += " ";
    }
    return resultado;
}

/**
 * Verifica si un símbolo ya existe en cualquier lugar de la pila (para defensa en vivo)
 */
bool simboloExisteEnPila(std::stack<std::string> copiaPila, const std::string& simbolo) {
    while (!copiaPila.empty()) {
        if (copiaPila.top() == simbolo) return true;
        copiaPila.pop();
    }
    return false;
}

/**
 * Convierte los tokens restantes desde el índice `idx` en una cadena para la traza
 */
std::string obtenerEntradaRestante(const std::vector<std::string>& tokens, size_t idx) {
    if (idx >= tokens.size()) return "lambda";
    std::string res;
    for (size_t i = idx; i < tokens.size(); ++i) {
        res += tokens[i];
        if (i + 1 < tokens.size()) res += " ";
    }
    return res;
}

/**
 * SIMULADOR DEL AUTÓMATA A PILA (APD)
 * Ejecuta el reconocimiento de la cadena mostrando la traza de configuraciones
 * instantáneas y diagnosticando errores léxicos/sintácticos detallados.
 */
bool simularAPD(const std::string& cadenaOriginal) {
    std::cout << "\n============================================================================\n";
    std::cout << "CADENA DE ENTRADA w: \"" << (cadenaOriginal.empty() ? "lambda (vacia)" : cadenaOriginal) << "\"\n";
    std::cout << "============================================================================\n";

    // 1. Fase de Análisis Léxico (Tokenización)
    ResultadoLexico lex = tokenizar(cadenaOriginal);
    if (!lex.exito) {
        std::cout << "\n[!] ERROR DETECTADO EN FASE LEXICA / PRE-PROCESAMIENTO:\n";
        std::cout << "    -> Tipo de Error : Error Tipo 4 (Token desconocido o mal formado)\n";
        std::cout << "    -> Token invalido: \"" << lex.tokenInvalido << "\" (posicion " << lex.posicionCaracter << ")\n";
        std::cout << "    -> Detalle       : " << lex.detalleError << "\n";
        std::cout << "\nRESULTADO: CADENA INVALIDA (w NO pertenece a L(M) y w NO pertenece a L(G)).\n";
        return false;
    }

    // Mostrar vector de tokens generado
    std::cout << "Tokens reconocidos (" << lex.tokens.size() << "): [ ";
    for (size_t i = 0; i < lex.tokens.size(); ++i) {
        std::cout << "\"" << lex.tokens[i] << "\"" << (i + 1 < lex.tokens.size() ? ", " : " ");
    }
    std::cout << "]\n\n";

    // Verificación opcional por si en la defensa piden rechazar la cadena vacía
    if (!ACEPTAR_CADENA_VACIA && lex.tokens.empty()) {
        std::cout << "[!] ERROR SINTACTICO:\n";
        std::cout << "    -> La configuracion actual rechaza la cadena vacia (lambda).\n";
        std::cout << "\nRESULTADO: CADENA INVALIDA (w NO pertenece a L(M) y w NO pertenece a L(G)).\n";
        return false;
    }

    // 2. Inicialización formal del Autómata a Pila
    std::stack<std::string> pila;
    pila.push("Z0"); // Símbolo inicial en el fondo de la pila
    std::string estado = "q0";

    std::cout << "TRAZA FORMAL DE CONFIGURACIONES INSTANTANEAS (Tope de pila a la izquierda):\n";
    std::cout << "----------------------------------------------------------------------------\n";

    // 3. Ciclo principal de procesamiento de la cadena de tokens
    for (size_t paso = 0; paso < lex.tokens.size(); ++paso) {
        const std::string& token = lex.tokens[paso];
        const std::string topeActual = pila.top();
        const std::string entradaRestante = obtenerEntradaRestante(lex.tokens, paso);
        const std::string pilaFormal = obtenerContenidoPilaFormal(pila);

        // Imprimir configuración instantánea antes de consumir el token
        std::cout << "Paso " << std::setw(2) << (paso + 1) << ": ("
                  << estado << ", " << entradaRestante << ", " << pilaFormal << ") |- ";

        // --------------------------------------------------------------------
        // CASO A: Etiqueta de Apertura (ej. <b>, <i>, <p>) -> Operación PUSH
        // --------------------------------------------------------------------
        if (APERTURA_A_PILA.count(token) > 0) {
            std::string nuevoSimbolo = APERTURA_A_PILA.at(token);

            // [Chequeo opcional Defensa en Vivo (d): Prohibir auto-anidamiento directo]
            if (PROHIBIR_AUTOANIDAMIENTO.count(nuevoSimbolo) > 0 && topeActual == nuevoSimbolo) {
                std::cout << "ERROR\n";
                std::cout << "\n[!] ERROR SINTACTICO EN PASO " << (paso + 1) << " (Token \"" << token << "\"):\n";
                std::cout << "    -> Restriccion de Anidamiento: No se permite anidar \"" << token
                          << "\" directamente dentro de otra etiqueta \"" << token << "\".\n";
                std::cout << "\nRESULTADO: CADENA INVALIDA (w NO pertenece a L(M) y w NO pertenece a L(G)).\n";
                return false;
            }

            // [Chequeo opcional Defensa en Vivo (e): Prohibir repetición en toda la pila]
            if (PROHIBIR_REPETICION_EN_TODA_LA_PILA && simboloExisteEnPila(pila, nuevoSimbolo)) {
                std::cout << "ERROR\n";
                std::cout << "\n[!] ERROR SINTACTICO EN PASO " << (paso + 1) << " (Token \"" << token << "\"):\n";
                std::cout << "    -> Restriccion de Anidamiento: La etiqueta \"" << token
                          << "\" ya se encuentra abierta previamente en la pila.\n";
                std::cout << "\nRESULTADO: CADENA INVALIDA (w NO pertenece a L(M) y w NO pertenece a L(G)).\n";
                return false;
            }

            pila.push(nuevoSimbolo);
            std::cout << "\n         [Regla: delta(q0, " << token << ", " << topeActual
                      << ") = {(q0, " << nuevoSimbolo << " " << topeActual << ")} -> PUSH " << nuevoSimbolo << "]\n";
        }
        // --------------------------------------------------------------------
        // CASO B: Etiqueta de Cierre (ej. </b>, </i>, </p>) -> Operación POP
        // --------------------------------------------------------------------
        else if (CIERRE_A_PILA.count(token) > 0) {
            const InfoCierre& info = CIERRE_A_PILA.at(token);

            // Error Tipo 1: Cierre sin apertura (Pila en Z0)
            if (topeActual == "Z0") {
                std::cout << "ERROR\n";
                std::cout << "\n[!] ERROR SINTACTICO EN PASO " << (paso + 1) << " (Token \"" << token << "\"):\n";
                std::cout << "    -> Tipo de Error: Error Tipo 1 (Cierre sin apertura / Pila en Z0)\n";
                std::cout << "    -> Detalle      : Se leyo la etiqueta de cierre \"" << token
                          << "\", pero la pila estaba en Z0 (no hay ninguna etiqueta abierta).\n";
                std::cout << "\nRESULTADO: CADENA INVALIDA (w NO pertenece a L(M) y w NO pertenece a L(G)).\n";
                return false;
            }

            // Error Tipo 2: Violación de orden LIFO / Mal anidamiento
            if (topeActual != info.simboloPilaEsperado) {
                std::cout << "ERROR\n";
                std::cout << "\n[!] ERROR SINTACTICO EN PASO " << (paso + 1) << " (Token \"" << token << "\"):\n";
                std::cout << "    -> Tipo de Error: Error Tipo 2 (Violacion de orden LIFO / Mal anidamiento)\n";
                std::cout << "    -> Detalle      : Se leyo \"" << token << "\" (que cierra a \"" << info.etiquetaApertura
                          << "\"), pero en el tope de la pila habia \"" << topeActual
                          << "\". Se esperaba cerrar primero el simbolo \"" << topeActual << "\".\n";
                std::cout << "\nRESULTADO: CADENA INVALIDA (w NO pertenece a L(M) y w NO pertenece a L(G)).\n";
                return false;
            }

            // Cierre correcto: desapilar el tope
            pila.pop();
            std::cout << "\n         [Regla: delta(q0, " << token << ", " << topeActual
                      << ") = {(q0, lambda)} -> POP " << topeActual << "]\n";
        }
        // --------------------------------------------------------------------
        // CASO C: Token TEXTO o Etiqueta Autocerrada -> Pila sin cambios
        // --------------------------------------------------------------------
        else if (token == "TEXTO" || ETIQUETAS_AUTOCERRADAS.count(token) > 0) {
            // [Chequeo opcional Defensa en Vivo (c): Prohibir TEXTO fuera de etiquetas]
            if (token == "TEXTO" && PROHIBIR_TEXTO_EN_RAIZ && topeActual == "Z0") {
                std::cout << "ERROR\n";
                std::cout << "\n[!] ERROR SINTACTICO EN PASO " << (paso + 1) << " (Token \"TEXTO\"):\n";
                std::cout << "    -> Restriccion Activa: No se permite TEXTO suelto fuera de etiquetas (Pila en Z0).\n";
                std::cout << "\nRESULTADO: CADENA INVALIDA (w NO pertenece a L(M) y w NO pertenece a L(G)).\n";
                return false;
            }

            std::cout << "\n         [Regla: delta(q0, " << token << ", " << topeActual
                      << ") = {(q0, " << topeActual << ")} -> Pila sin cambios]\n";
        }
    }

    // 4. Verificación al consumir toda la entrada (w = lambda)
    std::string pilaAlFinal = obtenerContenidoPilaFormal(pila);
    std::cout << "Final  : (q0, lambda, " << pilaAlFinal << ") |- ";

    // Error Tipo 3: Quedaron etiquetas abiertas en la pila al terminar la cadena
    if (pila.top() != "Z0") {
        std::cout << "ERROR\n";
        std::cout << "\n[!] ERROR SINTACTICO AL FINAL DE LA ENTRADA (Token lambda):\n";
        std::cout << "    -> Tipo de Error: Error Tipo 3 (Etiquetas sin cerrar al final de la entrada)\n";
        std::cout << "    -> Detalle      : Se consumio toda la cadena, pero el tope de la pila no es Z0.\n";
        std::cout << "    -> Simbolos sin cerrar en la pila (de tope a fondo): ";

        std::stack<std::string> copia = pila;
        while (!copia.empty() && copia.top() != "Z0") {
            std::cout << "[" << copia.top() << "] ";
            copia.pop();
        }
        std::cout << "\n\nRESULTADO: CADENA INVALIDA (w NO pertenece a L(M) y w NO pertenece a L(G)).\n";
        return false;
    }

    // Transición lambda al estado de aceptación qf
    estado = "qf";
    std::cout << "(qf, lambda, Z0)\n";
    std::cout << "         [Regla final: delta(q0, lambda, Z0) = {(qf, Z0)} -> ACEPTACION]\n";
    std::cout << "----------------------------------------------------------------------------\n";
    std::cout << "RESULTADO: CADENA VALIDA (w pertenece a L(M) y w pertenece a L(G)).\n";
    return true;
}

/**
 * Ejecuta la batería automática de casos de prueba obligatorios y de error
 */
void ejecutarCasosDePruebaConsigna() {
    struct CasoPrueba {
        std::string nombre;
        std::string cadena;
    };

    const std::vector<CasoPrueba> casos = {
        {"1. Cadena ALFA (Valida simple)",              "<p> TEXTO </p>"},
        {"2. Cadena BETA (Valida anidada)",             "<p> TEXTO <b> TEXTO </b> </p>"},
        {"3. Cadena GAMMA (Valida secuencial/anidada)", "<b> <i> TEXTO </i> <i> TEXTO </i> </b>"},
        {"4. Cadena VACIA lambda (Valida por defecto)", ""},
        {"5. Ejemplo Invalido Consigna (Error Tipo 2 - Mal anidamiento LIFO)", "<p><b>TEXTO</p></b>"},
        {"6. Prueba Error Tipo 1 (Cierre sin apertura / Pila en Z0)",          "TEXTO </p>"},
        {"7. Prueba Error Tipo 3 (Etiquetas sin cerrar al final)",             "<p> <b> TEXTO </b>"},
        {"8. Prueba Error Tipo 4 (Etiqueta desconocida fuera de Sigma)",       "<p> <u> TEXTO </u> </p>"}
    };

    for (const auto& caso : casos) {
        std::cout << "\n****************************************************************************\n";
        std::cout << "CASO DE PRUEBA: " << caso.nombre << "\n";
        simularAPD(caso.cadena);
    }
}

int main() {
    int opcion = -1;

    while (opcion != 0) {
        std::cout << "\n+--------------------------------------------------------------------------+\n";
        std::cout << "|       ANALIZADOR SINTACTICO MINIHTML - AUTOMATA A PILA (OPCION B)        |\n";
        std::cout << "+--------------------------------------------------------------------------+\n";
        std::cout << "| 1. Ejecutar bateria automatica (alfa, beta, gamma, vacia y 4 errores)    |\n";
        std::cout << "| 2. Ingresar una cadena MiniHTML manualmente por teclado (Defensa en vivo)|\n";
        std::cout << "| 0. Salir                                                                 |\n";
        std::cout << "+--------------------------------------------------------------------------+\n";
        std::cout << "Seleccione una opcion: ";

        std::string lineaOpcion;
        if (!std::getline(std::cin, lineaOpcion)) break;

        try {
            opcion = std::stoi(trim(lineaOpcion));
        } catch (...) {
            opcion = -1;
        }

        if (opcion == 1) {
            ejecutarCasosDePruebaConsigna();
        } else if (opcion == 2) {
            std::cout << "\nIngrese la cadena w a evaluar (deje vacio y presione Enter para lambda):\n> ";
            std::string entradaUsuario;
            std::getline(std::cin, entradaUsuario);
            simularAPD(entradaUsuario);
        } else if (opcion == 0) {
            std::cout << "Saliendo del simulador...\n";
        } else {
            std::cout << "Opcion no valida. Intente nuevamente.\n";
        }
    }

    return 0;
}