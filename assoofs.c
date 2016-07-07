#include <linux/module.h>    /* Needed by all modules */
#include <linux/kernel.h>    /* Needed for KERN_INFO */
#include <linux/init.h>      /* Needed for the macros */
#include <linux/pagemap.h>   /* PAGE_CACHE_SIZE */
#include <linux/fs.h>        /* libfs stuff */
#include <asm/atomic.h>      /* atomic_t stuff */
#include <asm/uaccess.h>     /* copy_to_user */
#include <linux/ctype.h>     /* is_alpha	*/	

#include <linux/slab.h>		 /* kmalloc stuff */

/**
 *  @file assoofs.c
 *	@brief Implementación de un modulo sencillo cargable en el kernel de Linux
 *	@author Jesus Cruz Olivera
 *
 *  @see Para acceder a un usuario con root 
 *  LabF5-x 
 *  AulaF52015 
 */  

#ifndef DOXYGEN_SHOULD_SKIP_THIS
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jesus Cruz Olivera");
#endif 


#define LFS_MAGIC 0x19980122															
#define TMPSIZE 50		/* número de cifras para representar el tamaño del contenido del file  */ 	
#define NFILES 50		/* número máximo de archivos en nuestro fs */																	

/* El contenido de un fichero es un contador, un texto con 50 caracteres y una variable de control*/
static struct assoofs_file_content {
	atomic_t counter; 		/* El contador del fichero */
	char text[TMPSIZE];		/* El texto del fichero */
	int contentText;		/* 1 Si hay texto en el archivo */
};

/* Los structs con el contenido de nuestro fs de los dos archivos iniciales */
struct assoofs_file_content contenido1; 
struct assoofs_file_content contenido2; 

/* El contador común a todos los ficheros nuevos creados con touch */
atomic_t contadorGlobal; 

/* Podemos crear unos 50 archivos, sus struct contenido serán guardados en este array global  */
struct assoofs_file_content archivos[NFILES];
/* La idea era usar pos como índice para acceder a el array de structs */
int pos = 0;

/**
 * @brief crea un inodo
 * @param sb es el superbloque del fs 
 * @param mode está definido en sys/stat.h, indica si es un directorio, un enlace ...
 */
static struct inode *assoofs_make_inode(struct super_block *sb, int mode) {
	struct inode *ret = new_inode(sb);

	if (ret) {
		ret->i_mode = mode;
		ret->i_uid.val = ret->i_gid.val = 0;
		ret->i_blocks = 0;
		ret->i_atime = ret->i_mtime = ret->i_ctime = CURRENT_TIME;
	}
	return ret;
}


/**
 * @brief abrir 
 */
static int assoofs_open(struct inode *inode, struct file *filp)
{
	filp->private_data = inode->i_private;
	return 0;
} 

/**
 * @brief: lee el contenido de un fichero y lo imprime por pantalla
 *
 * Al imprimir tiene en cuenta si el fichero tiene solo un contador o si además tiene texto
 */
static ssize_t assoofs_read_file(struct file *filp, char *buf,
		size_t count, loff_t *offset) { 
		
 	struct assoofs_file_content *content = (struct assoofs_file_content *)  filp->private_data;	
	atomic_t contadorAux = content->counter;	
	atomic_t *contador;
	contador = &contadorAux;

	int lenCounter,lenText,len,v;
	char counterString[TMPSIZE];

	v = atomic_read(contador);
	if (*offset > 0)
		v -= 1;  
	else
		atomic_inc(contador);
	lenCounter = snprintf(counterString, TMPSIZE, "%d\n", v);

	/* Si tiene texto tenemos que pasar el contador y el texto*/
	if ( content->contentText == 1 ){ 

		lenText = strlen(content->text);
		char textString[lenText];

		lenText = snprintf(textString, lenText, "%s\n", content->text);

		len = lenText + lenCounter ;
 
		char wholeString[len];
 
		strcpy(wholeString,counterString);
		strcat(wholeString,textString); 
		strcat(wholeString,"\n");

		printk(KERN_INFO "w:%s\n", wholeString);
	  
		if (*offset > len) 
			return 0;
		if (count > len - *offset)
			count = len - *offset;

		/* En buf copiamos el contenido de tmp */
		if (copy_to_user(buf, wholeString + *offset, /*count*/50))
			return -1;
	}

	/* Si no tiene texto solo tiene número, luego solo pasamos el contador */
	else {
		
		if (*offset > lenCounter)
			return 0;
		if (count > lenCounter - *offset)
			count = lenCounter - *offset;

		if (copy_to_user(buf,  counterString + *offset, count))
			return -1;
		
	}
	*offset += count;
 
	/* Al salir escribimos el valor incrementado del contador */
	content->counter = contadorAux;
	
	return count;
}



/**
 * @brief escribe un nuevo archivo e incrementa el contador 
 * Si lo que nos pasa el usuario empieza con una letra lo guardamos como un texto, si no es porque
 * empieza con un numero y lo guardamos como un numero/contador. No es seguro 
 */
static ssize_t assoofs_write_file(struct file *filp, const char *buf,size_t count,loff_t *offset) {
	 
	struct assoofs_file_content *content = (struct assoofs_file_content *)  filp->private_data;
	atomic_t *contador;
	char tmp[TMPSIZE]; 
	contador = &content->counter;
	
	memset(tmp, 0, TMPSIZE);
	if (copy_from_user(tmp, buf, count)) /* Copiamos desde el espacio de usuario */
		return -EFAULT;
	/* en tmp tengo lo que me pasa el usuario*/
	/* Comprobamos si es un número o texto */
	if ( !isalpha(tmp[0]) ){
		/* A la variable atómica contador le ponemos el valor del string tmp pasado a long */ 
		atomic_set(contador, simple_strtol(tmp, NULL, 10));
	}
	/* si es texto lo escribimos en text */
	else{
		char aux[50];
		memset(aux, ' ', 50);
		strcpy(aux, tmp); 
	 	strcpy(content->text,aux); 		/* Guardamos el texto */
		content->contentText = 1;		/* Marcamos que el struct contenido contiene texto */
	} 
	return count;  

}


// Crear un directorio 
static int assoofs_mkdir(struct inode *dir, struct dentry *dentry, umode_t mode);

// Crear archivo
static int assoofs_create(struct inode *dir, struct dentry *dentry, umode_t mode, bool excl);

struct dentry *assoofs_lookup(struct inode *parent_inode, struct dentry *child_dentry, 
	unsigned int flags);

static struct inode_operations assoofs_inode_ops = {
    .create = assoofs_create,
    .lookup = assoofs_lookup,
    .mkdir = assoofs_mkdir,
};
 
/**
 * @brief crea un directorio vacio
 */
static int assoofs_mkdir(struct inode *dir, struct dentry *dentry, umode_t mode){

	dir->i_op = &assoofs_inode_ops;										 
	dir->i_fop = &simple_dir_operations;	
	d_add(dentry,dir);
	return 0;
}

struct dentry *assoofs_lookup(struct inode *parent_inode, struct dentry *child_dentry,
	unsigned int flags) {

	printk(KERN_INFO "Looking up inode...\n");

    return NULL;
}

/* Las operaciones que soportan los ficheros*/
static struct file_operations assoofs_file_ops = {
	.open	= assoofs_open,
	.read 	= assoofs_read_file,
	.write  = assoofs_write_file,
};


/**
 * @brief crear un archivo nuevo con su contador correspondiente 
 * 
 * El struct contenido de cada fichero será guardado en el array global de structs contenido
 * Para ello usamos la variable global pos que será incrementada al crear un nuevo fichero
 * En el campo i_private guardamos la dirección a la posición marcada por pos en el array de
 * struct contenido en el momento de llamar a la función 
 */
static int assoofs_create(struct inode *dir, struct dentry *dentry, umode_t mode, bool excl) {
	struct inode *inode;

	inode = assoofs_make_inode(dentry->d_sb, S_IFREG | 0644);
	inode->i_fop = &assoofs_file_ops;  
	inode->i_private = &archivos[pos];
	archivos[pos].counter = contadorGlobal;	
	archivos[pos].contentText = 0;	
	d_add(dentry, inode); 
	pos = pos+1;
	return 0;	
}  

/*================================================================================================*/


/**
 * @brief crea un directorio en nuestro fs
 *
 * Es bastante parecida a \code assoofs_create_file \endcode solo que en este caso al 
 * estar creando un directorio a la función \code assoofs_make_inode \endcode le pasamos
 * S_IFDIR, además las operaciones serán las operaciones sobre ficheros
 */
static struct dentry *assoofs_create_dir (struct super_block *sb,
		struct dentry *parent, const char *name) {

	struct dentry *dentry;
	struct inode *inode;
	struct qstr qname;

	qname.name = name;
	qname.len = strlen (name);
	qname.hash = full_name_hash(name, qname.len);
	dentry = d_alloc(parent, &qname);
	if (! dentry)
		goto out;
	inode = assoofs_make_inode(sb, S_IFDIR | 0644);
	if (! inode)
		goto out_dput;

	assoofs_mkdir(inode,dentry,S_IFDIR); 
	return dentry;

	out_dput:
		return dentry;																	
 	out:
		return dentry;																			
}


/**
 *  @brief crea un archivo, en nuestro caso un contador
 *
 *	Los contadores de nuestra práctica son archivos en los cuales hemos guardado un contador que
 *  se irá incrementado cada vez que lo leamos. La función assoofs_create_file se encargará de 
 *  crear dicho archivo contador con su inodo
 *
 *  @param *sb Puntero a nuestro superbloque
 *  @param dentry Un dentry es la relación entre el nombre de un fichero y el inode
 *  @param *dir El directorio en el que estará el archivo
 *  @param *name El nombre que le queremos dar 
 *  @param *contador El contador que le pasamos, su valor inicial es 0 
 */
static struct dentry *assoofs_create_file ( struct super_block *sb, struct dentry *dir, const char 
*name, struct assoofs_file_content *contenido )
{
	struct dentry *dentry;
	struct inode *inode;
	struct qstr qname;

	qname.name = name;
	qname.len = strlen (name);
	qname.hash = full_name_hash(name, qname.len);

	dentry = d_alloc(dir, &qname);
	if (! dentry)
		goto out;
	inode = assoofs_make_inode(sb, S_IFREG | 0644);
	if (! inode)
		goto out_dput;

	inode->i_fop = &assoofs_file_ops;
	/* en void* i_private guardamos la dirección al struct contenido*/
	inode->i_private = contenido;

	d_add(dentry, inode);
	return dentry;

  out_dput:
	return 0;
  out:
	return 0;
}



/**
 * @brief Crear los contadores y la carpeta
 * 
 * Antes hemos inicializado el root
 */ 
static void assoofs_create_files (struct super_block *sb, struct dentry *root) {

	atomic_t contador1, contador2;	/* Un contador por cada fichero */
	struct dentry *carpeta1;		/* El dentry para la carpeta */

	atomic_set(&contador1, 0); 
	atomic_set(&contador2, 0); 
	 
	contenido1.counter = contador1;
	contenido2.counter = contador2;
	strcpy(contenido1.text,"");
	strcpy(contenido2.text,"");
	
	assoofs_create_file(sb, root, "contador1",&contenido1);			/* Estará en la ráiz*/

	carpeta1=assoofs_create_dir(sb, root, "carpeta1"); 

	assoofs_create_file(sb, carpeta1, "contador2",&contenido2);		/* Estará en la carpeta 1*/

	atomic_set(&contadorGlobal,0);						   /* Ponemos a 0 el contador global */
}



/**
 * @brief función con las operaciones que se pueden realizar con el superbloque 
 */
static struct super_operations assoofs_s_ops = {
	.statfs = simple_statfs,
	.drop_inode = generic_delete_inode,
};



/**
 * @brief Función encargada de configurar el fs 								
 */
static int assoofs_fill_super ( struct super_block *sb, void *data, int silent) {	
	struct inode *root;
	struct dentry *root_dentry;	

	/* Configuraciones básicas ,las macros son de linux/pagemap.h*/
	sb->s_blocksize = PAGE_CACHE_SIZE;
	sb->s_blocksize_bits = PAGE_CACHE_SHIFT;
	sb->s_magic = LFS_MAGIC;	/* Esta macro es una excepción, la hemos definido nosotros */
	sb->s_op = &assoofs_s_ops;	/* Las operaciones que se pueden realizar con el superbloque */

	/* Creación del nodo raíz, root es del tipo struct inode  */
	root = assoofs_make_inode (sb, S_IFDIR | 0755 );	/* Creamos el inodo */
	if (!root)											/* Si ha habido algún error salimos */
		goto out;
	//root->i_op = &simple_dir_inode_operations;
	root->i_op = &assoofs_inode_ops;
	root->i_fop=&simple_dir_operations;

	/* d_make_root está implementada en fs/dcache.c, lo añadimos a la cache de directorios  */
	root_dentry = d_make_root(root);
	if (!root_dentry)
		goto out_iput;
	sb->s_root = root_dentry;
	
	/* Después de crear el inodo creamos los archivos */
	assoofs_create_files (sb, root_dentry);
	return 0;

  out_iput:
	return -1;	
  out:
	return -1;				 													
}

/**
 * @brief Función que se ejecuta al registrar el sistema de ficheros,superbloque 
 * Llama a la función assoofs_fill_super 
 */ 
static struct dentry *assoofs_get_super( struct file_system_type *fst, int flags,
	const char *devname, void *data){
	return mount_bdev (fst, flags, devname, data, assoofs_fill_super);
}


/**
 * @brief función con la información del fs que pasamos a register_filesystem en assoofs_init 
 */
static struct file_system_type assoofs_type = {
	.owner = THIS_MODULE,			/* El dueño */
	.name ="assoofs",				/* El nombre */
	.mount = assoofs_get_super,		/* Al montar fs llamamos a get_super */
	.kill_sb = kill_litter_super,	/* Al eliminar el fs llamamos kill_little_super */
};


/** 
 * @brief Registramos en la capa vfs al cargarlo, nuestro programa empieza aqui
 * le pasamos un assoofs_type que es una estructura anteriormente declarada
 */
static int __init assoofs_init(void) {
    return register_filesystem(&assoofs_type);
}

/**
 * @brief borramos el módulo
 */
static void __exit assoofs_exit(void) {
    unregister_filesystem(&assoofs_type);
}

module_init(assoofs_init);
module_exit(assoofs_exit);
