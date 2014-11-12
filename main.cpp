#include <iostream>
#include <cstdlib>
#include <pthread.h>
#include <stdlib.h>
#include <cstdio>
#include <fstream>
#include <semaphore.h>
#include <vector>

using namespace std;
#define NUM_THREADS 5
const int tamanoMemoria = 1600;
const int tamanoInstrucciones = 768;
const int numRegistros = 32;

struct IfId{
	int npc;
	int ir[4];
};

struct IdEx{
   	int npc;
   	int a;
   	int b;
   	int immm;
   	int ir[4];
};

struct ExMem{
	int cond;
	int aluOutput;
	int b;
	int ir[4];
};

struct MemWb{
	int aluOutput;
	int lmd;
	int ir[4];
};

struct Cache{
    int bloques[8][4];
    int etiqueta[8];
    char estado[8];
};


int ciclo = 1;
int quantum = 0;
int pc = 0;
int etapasFinalizadas = 0;
int barrera = 4;
int hilosFinalizados = 0;
vector<int> memoria(tamanoMemoria, 1);
//registros inicializados en 1
//RL es el reg en la pos 31
vector<int> registros(numRegistros,1);
vector<int> respaldoHilos(34 * 5, -1);
IfId registroIfId = {0,{-1,-1,-1,-1}};
IdEx registroIdEx = {0,0,0,0,{-1,-1,-1,-1}};
ExMem registroExMem = {0,0,0,{-1,-1,-1,-1}};
MemWb registroMemWb = {0,0,{-1,-1,-1,-1}};
Cache cacheDatos;
sem_t semIf,semId,semEx,semMem,semWb,semMain,semRegistros,semEspereId, semEspereEx,
      semEspereMem,semEspereWb;
pthread_mutex_t mutex1 = PTHREAD_MUTEX_INITIALIZER;


void cargarInstrucciones(){
	int input;
	int i = 0;
	string c;
	cout<<"Escriba el nombre del archivo de instrucciones, respete mayusuculas"<<endl;
	bool salir;
    for(int j=0; j<1; j++){
        salir = false;
        while(!salir){
            getline(cin,c);
            const char *entrada = c.c_str();
            ifstream instrucciones(entrada);
            if(!instrucciones.is_open()){
                cerr<<"No se pudo abrir el archivo,intente de nuevo"<<endl;
            }
            else{
                while (instrucciones >> input && i < tamanoInstrucciones){
                    memoria[i] = input;
                    i++;
                }
                salir = true;
            }
        }
    }
    /**
    cout<<"Escriba el valor del quantum, debe ser positivo y diferente a 0"<<endl;
    salir = false;
    while(!salir){
    	getline(cin,c);
    	if(c == "0"){
    		cout<<"Escribio 0, Por favor escriba un valor diferente"<<endl;
    	}
    	else{
    	quantum = std::atoi(c.c_str());
    	cout<<"El tiempo quantum fue seteado en "<< quantum << " ciclos de reloj"<<endl;
    	salir = true;
    	}
    }
    **/
}

void inicializarSemaforos(){
    sem_init(&semIf,0,0);
    sem_init(&semId,0,0);
    sem_init(&semEx,0,0);
    sem_init(&semMem,0,0);
    sem_init(&semWb,0,0);
    sem_init(&semMain,0,0);
    sem_init(&semEspereId,0,0);
    sem_init(&semEspereEx,0,0);
    sem_init(&semEspereWb,0,0);
    sem_init(&semEspereMem,0,0);
    sem_init(&semRegistros,0,0);
}

//Un ciclo de reloj se simulara con hilos esperando cada uno un semaforo,
//una vez terminada su rutina.
void iniciarCicloReloj(){
	sem_post(&semIf);
	sem_post(&semId);
	sem_post(&semEx);
	sem_post(&semMem);
	sem_post(&semWb);
}

int loadCache(int numBloque){
    int indice = numBloque % 8;
    int indiceMemoria = numBloque * 4;
    bool salir = false;
    while(!salir){
        if(cacheDatos.etiqueta[indice] == numBloque){
            salir = true;
        }else{
        	///remplazo bloque, escribir en memoria
            int dirMemoriaViejo = cacheDatos.etiqueta[indice]*4;
            for(int i = 0;i<4;i++){
            	memoria[dirMemoriaViejo+i] = cacheDatos.bloques[indice][i];
            }


            cacheDatos.etiqueta[indice] = numBloque;
            for(int i=0; i<4; i++){
                cacheDatos.bloques[indice][i] = memoria[indiceMemoria + i];
            }
        }
    }
    return indice;
}

void *rutinaIF(void *args){
   bool suicidio = false;
   while(true){
       sem_wait(&semIf);
       sem_wait(&semEspereId);

       //cargar instruccion en IF/ID
       //Ahora hay que usar la cache en vez de entrar a memoria
       for(int i = 0; i < 4; i++){
           registroIfId.ir[i] = memoria[pc+i];
       }

       if(registroIfId.ir[0] == 63){
           suicidio = true;
       }

       pc += 4;
       registroIfId.npc = pc;

       pthread_mutex_lock( &mutex1 );

       if(suicidio){
           hilosFinalizados++;
       }

       if(etapasFinalizadas < barrera){
           etapasFinalizadas++;
       }
       else{

          sem_post(&semMain);
       }
       pthread_mutex_unlock( &mutex1 );

       if(suicidio){
          pthread_exit(NULL);
       }
    }
   return 0;
}

void *rutinaID(void *args){
    int aTemp = -1;
    int bTemp = -1;
    int immTemp = -1;


    while(true){
        switch(registroIfId.ir[0]){
            case 63:
                sem_post(&semEspereId);
                pthread_mutex_lock( &mutex1 );
                hilosFinalizados++;
                if(etapasFinalizadas < barrera){
                    etapasFinalizadas++;
                }else{
                    sem_post(&semMain);
                }
                pthread_mutex_unlock( &mutex1 );
                pthread_exit(NULL); //Muerase
                break;
            case 32:
                aTemp = registros[registroIfId.ir[1]];
                bTemp = registros[registroIfId.ir[2]];
                break;
            case 8:
                aTemp = registros[registroIfId.ir[1]];
                immTemp = registroIfId.ir[3];
                break;
            case 34:
                aTemp = registros[registroIfId.ir[1]];
                bTemp = registros[registroIfId.ir[2]];
                break;
            case 12:
                aTemp = registros[registroIfId.ir[1]];
                bTemp = registros[registroIfId.ir[2]];
                break;
            case  14:
                aTemp = registros[registroIfId.ir[1]];
                bTemp = registros[registroIfId.ir[2]];
                break;
            case 35:
                aTemp = registros[registroIfId.ir[1]];
                immTemp = registroIfId.ir[3];
                break;
            case 43:
                aTemp = registros[registroIfId.ir[1]];
                immTemp = registroIfId.ir[3];
                bTemp = registros[registroIfId.ir[2]];
                break;

        }
        sem_wait(&semId);
        sem_wait(&semEspereEx);
        sem_wait(&semRegistros);

        registroIdEx.a = aTemp;
        registroIdEx.b = bTemp;
        registroIdEx.immm = immTemp;

        for(int i = 0; i < 4; i++){
            registroIdEx.ir[i] = registroIfId.ir[i];
        }

        registroIdEx.npc = registroIfId.npc;


        sem_post(&semEspereId);

        pthread_mutex_lock( &mutex1 );

        if(etapasFinalizadas < 4){
            etapasFinalizadas++;
        }else{
            sem_post(&semMain);
        }
        pthread_mutex_unlock( &mutex1 );

    }

    pthread_exit(NULL);
}

void *rutinaEX(void *args){
      int aluOutputTemp = -1;
      int bTemp = -1;

      while(true){
			switch(registroExMem.ir[0]){
                  case 63:
                      pthread_mutex_lock( &mutex1 );
                      hilosFinalizados++;
                      if(etapasFinalizadas < barrera){
                          etapasFinalizadas++;
                      }
                      else{
                          sem_post(&semMain);
                      }
                      pthread_mutex_unlock( &mutex1 );
                      sem_post(&semEspereEx);
                      pthread_exit(NULL); //Muerase
                      break;
                  case 32:
                      aluOutputTemp = registroIdEx.a + registroIdEx.b;
                      break;
                  case 8:
                      aluOutputTemp = registroIdEx.a + registroIdEx.immm;
                      break;
                  case 34:
                      aluOutputTemp = registroIdEx.a - registroIdEx.b;
                      break;
                  case 12:
                      aluOutputTemp = registroIdEx.a * registroIdEx.b;
                      break;
                  case  14:
                      aluOutputTemp = registroIdEx.a / registroIdEx.b;
                      break;
                  case 35:
                      aluOutputTemp = registroIdEx.a + registroIdEx.immm;
                      break;
                  case 43:
                      aluOutputTemp = registroIdEx.a + registroIdEx.immm;
                      bTemp = registroIdEx.b;
                      break;
            }

			sem_wait(&semEx);
			sem_wait(&semEspereMem);

			registroExMem.aluOutput = aluOutputTemp;
			registroExMem.b = bTemp;

			for(int i = 0; i < 4; i++){
				registroExMem.ir[i] = registroIdEx.ir[i];
			}


			sem_post(&semEspereEx);

			pthread_mutex_lock( &mutex1 );

			if(etapasFinalizadas < 4){
		         etapasFinalizadas++;
			}
			else{
			    sem_post(&semMain);
		    }
			pthread_mutex_unlock( &mutex1 );
    }
    return 0;
}


void *rutinaMEM(void *args){
    bool suicidio = false;
	while(true){
	    sem_wait(&semMem);
	    sem_wait(&semEspereWb);

        for(int i = 0; i < 4; i++){
        	registroMemWb.ir[i] = registroExMem.ir[i];
        }

        if(registroMemWb.ir[0] == 63){
        	suicidio = true;
        }

        //Actualizacion del ALUOutput
        registroMemWb.aluOutput = registroExMem.aluOutput;

        //Cambiar a cache de datos,operacion load
        if(registroMemWb.ir[0] == 35){
        	//calcular Bloque
        	int posEnCache = loadCache((tamanoInstrucciones + registroExMem.aluOutput)/4);
        	registroMemWb.lmd = cacheDatos.bloques[posEnCache][(tamanoInstrucciones + registroExMem.aluOutput)%4];
        }


        //Cambiar a cache de datos
        //operacion Store
        else if(registroMemWb.ir[0] == 43){
        	int posEnCache = loadCache((tamanoInstrucciones + registroMemWb.aluOutput)/4);
        	cacheDatos.bloques[posEnCache][(tamanoInstrucciones + registroMemWb.aluOutput)%4] = registroExMem.b;
        	//memoria[768 + registroMemWb.aluOutput] = registroExMem.b;


        }

        sem_post(&semEspereMem);

        pthread_mutex_lock( &mutex1 );
        if(suicidio){
        	hilosFinalizados++;
        }

	    if(etapasFinalizadas < barrera){
            etapasFinalizadas++;
	    }
        else{
	        sem_post(&semMain);
	    }

	    pthread_mutex_unlock( &mutex1 );

	    if(suicidio){
	    	pthread_exit(NULL); //Muerase
	    }
    }
	return 0;
}

void *rutinaWB(void *args){
    while(true){
	    sem_wait(&semWb);

	    //Operaciones registro immm
	    if(registroMemWb.ir[0] == 8){

	        registros[registroMemWb.ir[2]] = registroMemWb.aluOutput;
	    }
	    //Operaciones registro registro
	    else if(registroMemWb.ir[0] == 32 || registroMemWb.ir[0] == 34 || registroMemWb.ir[0] == 12 ||
	 	       registroMemWb.ir[0] == 14){

	    	registros[registroMemWb.ir[3]] = registroMemWb.aluOutput;
	    }
	    //Operacion Load
	    else if(registroMemWb.ir[0] == 35){

	    	registros[registroMemWb.ir[2]] = registroMemWb.lmd;
	    }


	    sem_post(&semRegistros);
	    sem_post(&semEspereWb);

	    pthread_mutex_lock( &mutex1 );

	    if(etapasFinalizadas < barrera){
            etapasFinalizadas++;
	    }
	    else{
	        sem_post(&semMain);
	    }

	    pthread_mutex_unlock( &mutex1 );

    }

    pthread_exit(NULL);
}

int main (){
	pthread_t threads[NUM_THREADS];
    bool finalizarEjecucion = false;

    //Inicializar Cache
     for(int i = 0; i < 8; i++ ){
    	for(int j = 0; j < 4 ; j++){
    		cacheDatos.bloques[i][j] = 0;
    	}
    	cacheDatos.estado[i] = -1;
    	cacheDatos.etiqueta[i] = 'c';
    }


    //cargar instrucciones
    cargarInstrucciones();

	//iniciar semaforos de cada etapa del pipeline, para controlar ciclos de reloj.
	inicializarSemaforos();

	//crearHilos
	pthread_create(&threads[0], NULL, rutinaIF,NULL);
	pthread_create(&threads[1], NULL, rutinaID,NULL);
	pthread_create(&threads[2], NULL, rutinaEX,NULL);
	pthread_create(&threads[3], NULL, rutinaMEM,NULL);
	pthread_create(&threads[4], NULL, rutinaWB,NULL);


	while(!finalizarEjecucion){
		iniciarCicloReloj();
		sem_wait(&semMain);
        ciclo++;
        etapasFinalizadas = 0;
        barrera -= hilosFinalizados;
        if(registroMemWb.ir[0] == 63){
		   finalizarEjecucion = true;
        }
	}

    printf("Fin de la ejecución del programa:\n");
    printf("\nRegistros: \n");
    for(int i=0; i<32; i++){
        printf("\tR%d = %d\n", i, registros[i]);
    }
    printf("\nMemoria: \n");
    for(int i=768; i<1600; i++){
        printf("\tM%d = %d\n", i, memoria[i]);
    }

    cout<<"Cache:"<<endl;
    for(int i = 0; i < 8; i++ ){
    	for(int j = 0; j < 4 ; j++){
    		cout<<cacheDatos.bloques[i][j]<< " ";
    	}
    	cout<<endl;
    }



    printf("\nCiclos de reloj: %d\n", ciclo);
	exit(0);

}









