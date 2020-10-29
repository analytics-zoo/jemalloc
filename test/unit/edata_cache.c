#include "test/jemalloc_test.h"

#include "jemalloc/internal/edata_cache.h"

static void
test_edata_cache_init(edata_cache_t *edata_cache) {
	base_t *base = base_new(TSDN_NULL, /* ind */ 1,
	    &ehooks_default_extent_hooks);
	assert_ptr_not_null(base, "");
	bool err = edata_cache_init(edata_cache, base);
	assert_false(err, "");
}

static void
test_edata_cache_destroy(edata_cache_t *edata_cache) {
	base_delete(TSDN_NULL, edata_cache->base);
}

TEST_BEGIN(test_edata_cache) {
	edata_cache_t ec;
	test_edata_cache_init(&ec);

	/* Get one */
	edata_t *ed1 = edata_cache_get(TSDN_NULL, &ec);
	assert_ptr_not_null(ed1, "");

	/* Cache should be empty */
	assert_zu_eq(atomic_load_zu(&ec.count, ATOMIC_RELAXED), 0, "");

	/* Get another */
	edata_t *ed2 = edata_cache_get(TSDN_NULL, &ec);
	assert_ptr_not_null(ed2, "");

	/* Still empty */
	assert_zu_eq(atomic_load_zu(&ec.count, ATOMIC_RELAXED), 0, "");

	/* Put one back, and the cache should now have one item */
	edata_cache_put(TSDN_NULL, &ec, ed1);
	assert_zu_eq(atomic_load_zu(&ec.count, ATOMIC_RELAXED), 1, "");

	/* Reallocating should reuse the item, and leave an empty cache. */
	edata_t *ed1_again = edata_cache_get(TSDN_NULL, &ec);
	assert_ptr_eq(ed1, ed1_again, "");
	assert_zu_eq(atomic_load_zu(&ec.count, ATOMIC_RELAXED), 0, "");

	test_edata_cache_destroy(&ec);
}
TEST_END

TEST_BEGIN(test_edata_cache_small_simple) {
	edata_cache_t ec;
	edata_cache_small_t ecs;

	test_edata_cache_init(&ec);
	edata_cache_small_init(&ecs, &ec);

	edata_t *ed1 = edata_cache_small_get(TSDN_NULL, &ecs);
	expect_ptr_not_null(ed1, "");
	expect_zu_eq(ecs.count, 0, "");
	expect_zu_eq(atomic_load_zu(&ec.count, ATOMIC_RELAXED), 0, "");

	edata_t *ed2 = edata_cache_small_get(TSDN_NULL, &ecs);
	expect_ptr_not_null(ed2, "");
	expect_zu_eq(ecs.count, 0, "");
	expect_zu_eq(atomic_load_zu(&ec.count, ATOMIC_RELAXED), 0, "");

	edata_cache_small_put(TSDN_NULL, &ecs, ed1);
	expect_zu_eq(ecs.count, 1, "");
	expect_zu_eq(atomic_load_zu(&ec.count, ATOMIC_RELAXED), 0, "");

	edata_cache_small_put(TSDN_NULL, &ecs, ed2);
	expect_zu_eq(ecs.count, 2, "");
	expect_zu_eq(atomic_load_zu(&ec.count, ATOMIC_RELAXED), 0, "");

	/* LIFO ordering. */
	expect_ptr_eq(ed2, edata_cache_small_get(TSDN_NULL, &ecs), "");
	expect_zu_eq(ecs.count, 1, "");
	expect_zu_eq(atomic_load_zu(&ec.count, ATOMIC_RELAXED), 0, "");

	expect_ptr_eq(ed1, edata_cache_small_get(TSDN_NULL, &ecs), "");
	expect_zu_eq(ecs.count, 0, "");
	expect_zu_eq(atomic_load_zu(&ec.count, ATOMIC_RELAXED), 0, "");

	test_edata_cache_destroy(&ec);
}
TEST_END

TEST_BEGIN(test_edata_cache_fill) {
	edata_cache_t ec;
	edata_cache_small_t ecs;

	test_edata_cache_init(&ec);
	edata_cache_small_init(&ecs, &ec);

	edata_t *allocs[EDATA_CACHE_SMALL_FILL * 2];

	/*
	 * If the fallback cache can't satisfy the request, we shouldn't do
	 * extra allocations until compelled to.  Put half the fill goal in the
	 * fallback.
	 */
	for (int i = 0; i < EDATA_CACHE_SMALL_FILL / 2; i++) {
		allocs[i] = edata_cache_get(TSDN_NULL, &ec);
	}
	for (int i = 0; i < EDATA_CACHE_SMALL_FILL / 2; i++) {
		edata_cache_put(TSDN_NULL, &ec, allocs[i]);
	}
	expect_zu_eq(EDATA_CACHE_SMALL_FILL / 2,
	    atomic_load_zu(&ec.count, ATOMIC_RELAXED), "");

	allocs[0] = edata_cache_small_get(TSDN_NULL, &ecs);
	expect_zu_eq(EDATA_CACHE_SMALL_FILL / 2 - 1, ecs.count,
	    "Should have grabbed all edatas available but no more.");

	for (int i = 1; i < EDATA_CACHE_SMALL_FILL / 2; i++) {
		allocs[i] = edata_cache_small_get(TSDN_NULL, &ecs);
		expect_ptr_not_null(allocs[i], "");
	}
	expect_zu_eq(0, ecs.count, "");

	/* When forced, we should alloc from the base. */
	edata_t *edata = edata_cache_small_get(TSDN_NULL, &ecs);
	expect_ptr_not_null(edata, "");
	expect_zu_eq(0, ecs.count, "Allocated more than necessary");
	expect_zu_eq(0, atomic_load_zu(&ec.count, ATOMIC_RELAXED),
	    "Allocated more than necessary");

	/*
	 * We should correctly fill in the common case where the fallback isn't
	 * exhausted, too.
	 */
	for (int i = 0; i < EDATA_CACHE_SMALL_FILL * 2; i++) {
		allocs[i] = edata_cache_get(TSDN_NULL, &ec);
		expect_ptr_not_null(allocs[i], "");
	}
	for (int i = 0; i < EDATA_CACHE_SMALL_FILL * 2; i++) {
		edata_cache_put(TSDN_NULL, &ec, allocs[i]);
	}

	allocs[0] = edata_cache_small_get(TSDN_NULL, &ecs);
	expect_zu_eq(EDATA_CACHE_SMALL_FILL - 1, ecs.count, "");
	expect_zu_eq(EDATA_CACHE_SMALL_FILL,
	    atomic_load_zu(&ec.count, ATOMIC_RELAXED), "");
	for (int i = 1; i < EDATA_CACHE_SMALL_FILL; i++) {
		expect_zu_eq(EDATA_CACHE_SMALL_FILL - i, ecs.count, "");
		expect_zu_eq(EDATA_CACHE_SMALL_FILL,
		    atomic_load_zu(&ec.count, ATOMIC_RELAXED), "");
		allocs[i] = edata_cache_small_get(TSDN_NULL, &ecs);
		expect_ptr_not_null(allocs[i], "");
	}
	expect_zu_eq(0, ecs.count, "");
	expect_zu_eq(EDATA_CACHE_SMALL_FILL,
	    atomic_load_zu(&ec.count, ATOMIC_RELAXED), "");

	allocs[0] = edata_cache_small_get(TSDN_NULL, &ecs);
	expect_zu_eq(EDATA_CACHE_SMALL_FILL - 1, ecs.count, "");
	expect_zu_eq(0, atomic_load_zu(&ec.count, ATOMIC_RELAXED), "");
	for (int i = 1; i < EDATA_CACHE_SMALL_FILL; i++) {
		expect_zu_eq(EDATA_CACHE_SMALL_FILL - i, ecs.count, "");
		expect_zu_eq(0, atomic_load_zu(&ec.count, ATOMIC_RELAXED), "");
		allocs[i] = edata_cache_small_get(TSDN_NULL, &ecs);
		expect_ptr_not_null(allocs[i], "");
	}
	expect_zu_eq(0, ecs.count, "");
	expect_zu_eq(0, atomic_load_zu(&ec.count, ATOMIC_RELAXED), "");

	test_edata_cache_destroy(&ec);
}
TEST_END

TEST_BEGIN(test_edata_cache_flush) {
	edata_cache_t ec;
	edata_cache_small_t ecs;

	test_edata_cache_init(&ec);
	edata_cache_small_init(&ecs, &ec);

	edata_t *allocs[2 * EDATA_CACHE_SMALL_MAX + 2];
	for (int i = 0; i < 2 * EDATA_CACHE_SMALL_MAX + 2; i++) {
		allocs[i] = edata_cache_get(TSDN_NULL, &ec);
		expect_ptr_not_null(allocs[i], "");
	}
	for (int i = 0; i < EDATA_CACHE_SMALL_MAX; i++) {
		edata_cache_small_put(TSDN_NULL, &ecs, allocs[i]);
		expect_zu_eq(i + 1, ecs.count, "");
		expect_zu_eq(0, atomic_load_zu(&ec.count, ATOMIC_RELAXED), "");
	}
	edata_cache_small_put(TSDN_NULL, &ecs, allocs[EDATA_CACHE_SMALL_MAX]);
	expect_zu_eq(0, ecs.count, "");
	expect_zu_eq(EDATA_CACHE_SMALL_MAX + 1,
	    atomic_load_zu(&ec.count, ATOMIC_RELAXED), "");

	for (int i = EDATA_CACHE_SMALL_MAX + 1;
	    i < 2 * EDATA_CACHE_SMALL_MAX + 1; i++) {
		edata_cache_small_put(TSDN_NULL, &ecs, allocs[i]);
		expect_zu_eq(i - EDATA_CACHE_SMALL_MAX, ecs.count, "");
		expect_zu_eq(EDATA_CACHE_SMALL_MAX + 1,
		    atomic_load_zu(&ec.count, ATOMIC_RELAXED), "");
	}
	edata_cache_small_put(TSDN_NULL, &ecs, allocs[2 * EDATA_CACHE_SMALL_MAX + 1]);
	expect_zu_eq(0, ecs.count, "");
	expect_zu_eq(2 * EDATA_CACHE_SMALL_MAX + 2,
	    atomic_load_zu(&ec.count, ATOMIC_RELAXED), "");

	test_edata_cache_destroy(&ec);
}
TEST_END

TEST_BEGIN(test_edata_cache_disable) {
	edata_cache_t ec;
	edata_cache_small_t ecs;

	test_edata_cache_init(&ec);
	edata_cache_small_init(&ecs, &ec);

	for (int i = 0; i < EDATA_CACHE_SMALL_FILL; i++) {
		edata_t *edata = edata_cache_get(TSDN_NULL, &ec);
		expect_ptr_not_null(edata, "");
		edata_cache_small_put(TSDN_NULL, &ecs, edata);
	}

	expect_zu_eq(EDATA_CACHE_SMALL_FILL, ecs.count, "");
	expect_zu_eq(0, atomic_load_zu(&ec.count, ATOMIC_RELAXED), "");

	edata_cache_small_disable(TSDN_NULL, &ecs);

	expect_zu_eq(0, ecs.count, "");
	expect_zu_eq(EDATA_CACHE_SMALL_FILL,
	    atomic_load_zu(&ec.count, ATOMIC_RELAXED), "Disabling should flush");

	edata_t *edata = edata_cache_small_get(TSDN_NULL, &ecs);
	expect_zu_eq(0, ecs.count, "");
	expect_zu_eq(EDATA_CACHE_SMALL_FILL - 1,
	    atomic_load_zu(&ec.count, ATOMIC_RELAXED),
	    "Disabled ecs should forward on get");

	edata_cache_small_put(TSDN_NULL, &ecs, edata);
	expect_zu_eq(0, ecs.count, "");
	expect_zu_eq(EDATA_CACHE_SMALL_FILL,
	    atomic_load_zu(&ec.count, ATOMIC_RELAXED),
	    "Disabled ecs should forward on put");

	test_edata_cache_destroy(&ec);
}
TEST_END

int
main(void) {
	return test(
	    test_edata_cache,
	    test_edata_cache_small_simple,
	    test_edata_cache_fill,
	    test_edata_cache_flush,
	    test_edata_cache_disable);
}
